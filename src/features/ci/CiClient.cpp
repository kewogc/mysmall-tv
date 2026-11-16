#include "CiClient.h"
#if WITH_CI
#include "Platform.h"
#include "CiParse.h"
#include <HTTPClient.h>
#include <LittleFS.h>
#include <PNGdec.h>
#include <atomic>
#include <memory>

using namespace smalltv;

namespace {

const uint32_t kHttpTimeoutMs  = 8000;
const int      kMaxReplyBytes  = 48 * 1024;   // a list of builds; each brings its commit message
const int      kMaxAvatarBytes = 400 * 1024;  // the picture as the server sends it
const uint32_t kMinFreeHeap    = 60000;       // a TLS session plus the PNG decoder
const size_t   kTaskStack      = 12288;
const char     kDir[]          = "/ci";

CiTracker g_tracker;

// One poll, built by the main loop, filled by the task, handed back whole.
struct Job {
  String   url, token;
  CiBuild  known[kCiMaxBuilds];   // the builds the screen has as running
  uint8_t  knownCount = 0;
  uint32_t badAvatars[8];         // pictures not to ask for again
  uint8_t  badCount = 0;
  String   avatarUrl;             // the picture the task went to fetch, if any
  bool     avatarOnly = false;    // fetch avatarUrl and do nothing else (the demo)
  uint32_t generation = 0;        // of the settings it was started under
  CiSnapshot snapshot;
  bool     ok = false;
  bool     avatarStored = false, avatarFailed = false;
  char     error[48] = {0};
};

std::atomic<Job*> g_result{nullptr};
bool     g_busy = false;
bool     g_polled = false, g_ok = false;
char     g_error[48] = {0};
uint32_t g_lastStartMs = 0, g_lastDoneMs = 0;
uint32_t g_generation = 0;
bool     g_started = false;

// ---- avatar files ---------------------------------------------------------
uint32_t urlHash(const char* url) {   // FNV-1a
  uint32_t h = 2166136261u;
  for (; *url; ++url) h = (h ^ uint8_t(*url)) * 16777619u;
  return h;
}
String avatarPath(uint32_t hash) {
  char b[24];
  snprintf(b, sizeof(b), "%s/%08lx.565", kDir, (unsigned long)hash);
  return String(b);
}
// Pictures that could not be fetched or decoded: not asked for again until a
// reboot, so a broken one does not cost a download on every poll.
uint32_t g_badAvatars[8] = {0};
uint8_t  g_badCount = 0;

// ---- HTTP in the task -----------------------------------------------------
// A connection for `url`: TLS without certificate checks for https (the device
// has no CA store), plain otherwise.
std::unique_ptr<WiFiClient> makeClient(const String& url) {
  if (url.startsWith("https://")) {
    SecureClient* secure = new SecureClient();
    secure->setInsecure();
    secure->setHandshakeTimeout(kHttpTimeoutMs / 1000);
    return std::unique_ptr<WiFiClient>(secure);
  }
  return std::unique_ptr<WiFiClient>(new WiFiClient());
}

// GET `url` with the token; the body in `body`. Returns the HTTP status, or a
// negative HTTPClient error.
int getJson(HTTPClient& http, WiFiClient& client, const String& url, const String& token, String& body) {
  body = "";
  http.setTimeout(kHttpTimeoutMs);
  http.setReuse(true);             // the detail requests ride the same connection
  http.setUserAgent(F(FW_NAME));
  if (!http.begin(client, url)) return -1;
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    if (http.getSize() > kMaxReplyBytes) code = -2;
    else body = http.getString();
  }
  http.end();
  return code;
}

void setError(Job& job, const char* what, int code) {
  if (code > 0) snprintf(job.error, sizeof(job.error), "%s: HTTP %d", what, code);
  else if (code == -2) snprintf(job.error, sizeof(job.error), "%s: reply too large", what);
  else snprintf(job.error, sizeof(job.error), "%s: no connection", what);
}

// ---- PNG to a small RGB565 square -----------------------------------------
// The picture is averaged down, not sampled: a 290-pixel avatar at 96 pixels
// would otherwise lose two rows and columns in three.
struct Scaler {
  PNG*      png;
  uint16_t* out;                      // CI_AVATAR_BIG squared
  int       width, height;
  int       row;                      // destination row being gathered, -1 at the start
  uint32_t  r[CI_AVATAR_BIG], g[CI_AVATAR_BIG], b[CI_AVATAR_BIG];
  uint16_t  n[CI_AVATAR_BIG];
  uint16_t  line[320];

  void flush() {
    if (row < 0 || row >= CI_AVATAR_BIG) return;
    for (int x = 0; x < CI_AVATAR_BIG; ++x) {
      uint16_t px = 0;
      if (n[x]) px = uint16_t(((r[x] / n[x]) << 11) | ((g[x] / n[x]) << 5) | (b[x] / n[x]));
      out[row * CI_AVATAR_BIG + x] = px;
    }
  }
};

void* pngOpen(const char* name, int32_t* size) {
  File* f = new File(LittleFS.open(name, "r"));
  if (!*f) { delete f; return nullptr; }
  *size = int32_t(f->size());
  return f;
}
void pngClose(void* handle) {
  File* f = static_cast<File*>(handle);
  f->close();
  delete f;
}
int32_t pngRead(PNGFILE* file, uint8_t* buffer, int32_t length) {
  File* f = static_cast<File*>(file->fHandle);
  int32_t n = int32_t(f->read(buffer, size_t(length)));
  file->iPos = int32_t(f->position());
  return n;
}
int32_t pngSeek(PNGFILE* file, int32_t position) {
  File* f = static_cast<File*>(file->fHandle);
  f->seek(uint32_t(position));
  file->iPos = position;
  return position;
}
int pngDraw(PNGDRAW* draw) {
  Scaler* s = static_cast<Scaler*>(draw->pUser);
  const int row = draw->y * CI_AVATAR_BIG / s->height;
  if (row != s->row) {
    s->flush();
    s->row = row;
    memset(s->r, 0, sizeof(s->r));
    memset(s->g, 0, sizeof(s->g));
    memset(s->b, 0, sizeof(s->b));
    memset(s->n, 0, sizeof(s->n));
  }
  // A transparent picture is laid over black, the screen's background.
  s->png->getLineAsRGB565(draw, s->line, PNG_RGB565_LITTLE_ENDIAN, 0x000000);
  for (int x = 0; x < s->width; ++x) {
    const int col = x * CI_AVATAR_BIG / s->width;
    const uint16_t px = s->line[x];
    s->r[col] += px >> 11;
    s->g[col] += (px >> 5) & 0x3F;
    s->b[col] += px & 0x1F;
    ++s->n[col];
  }
  if ((draw->y & 31) == 31) delay(1);   // let the other tasks in
  return 1;
}

bool decodeAvatar(const char* pngPath, const String& outPath) {
  std::unique_ptr<PNG> png(new (std::nothrow) PNG());            // about 45 KB
  std::unique_ptr<Scaler> scaler(new (std::nothrow) Scaler());
  std::unique_ptr<uint16_t[]> out(new (std::nothrow) uint16_t[CI_AVATAR_BIG * CI_AVATAR_BIG]);
  if (!png || !scaler || !out) return false;
  if (png->open(pngPath, pngOpen, pngClose, pngRead, pngSeek, pngDraw) != PNG_SUCCESS) return false;
  bool ok = false;
  const int w = png->getWidth(), h = png->getHeight();
  // The decoder's line buffer holds 320 pixels; a larger picture cannot be read.
  if (w >= 1 && w <= 320 && h >= 1 && h <= 2048) {
    memset(out.get(), 0, CI_AVATAR_BIG * CI_AVATAR_BIG * 2);
    scaler->png = png.get();
    scaler->out = out.get();
    scaler->width = w;
    scaler->height = h;
    scaler->row = -1;
    ok = png->decode(scaler.get(), 0) == PNG_SUCCESS;
    scaler->flush();
  }
  png->close();
  if (!ok) return false;
  File f = LittleFS.open(outPath, "w");
  if (!f) return false;
  const size_t bytes = CI_AVATAR_BIG * CI_AVATAR_BIG * 2;
  const bool written = f.write(reinterpret_cast<const uint8_t*>(out.get()), bytes) == bytes;
  f.close();
  if (!written) LittleFS.remove(outPath);
  return written;
}

// Keep the folder small: past 24 pictures, start over.
void trimAvatars() {
  File dir = LittleFS.open(kDir);
  if (!dir || !dir.isDirectory()) return;
  int count = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) ++count;
  if (count < 24) return;
  dir.rewindDirectory();
  String names[32];
  int n = 0;
  for (File f = dir.openNextFile(); f && n < 32; f = dir.openNextFile()) names[n++] = String(kDir) + "/" + f.name();
  for (int i = 0; i < n; ++i) LittleFS.remove(names[i]);
}

bool fetchAvatar(const String& url) {
  if (!(url.startsWith("https://") || url.startsWith("http://"))) return false;
  if (LittleFS.totalBytes() - LittleFS.usedBytes() < size_t(kMaxAvatarBytes) + 64 * 1024) return false;
  LittleFS.mkdir(kDir);
  trimAvatars();
  const String tmp = String(kDir) + "/avatar.tmp";
  bool got = false;
  {
    std::unique_ptr<WiFiClient> client = makeClient(url);
    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    http.setUserAgent(F(FW_NAME));
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (client && http.begin(*client, url)) {
      if (http.GET() == HTTP_CODE_OK && http.getSize() <= kMaxAvatarBytes) {
        File f = LittleFS.open(tmp, "w");
        if (f) {
          got = http.writeToStream(&f) > 0;
          f.close();
        }
      }
      http.end();
    }
  }   // the connection is closed before the decoder takes its memory
  bool ok = got && decodeAvatar(tmp.c_str(), avatarPath(urlHash(url.c_str())));
  LittleFS.remove(tmp);
  return ok;
}

// ---- the poll --------------------------------------------------------------
// The builds, over one connection. `http` is declared after `client` on
// purpose: it holds a pointer to the client and stops it in its destructor, so
// it has to go first. Freeing the client by hand while `http` was still alive
// crashed the device.
void pollBuilds(Job& job) {
  std::unique_ptr<WiFiClient> client = makeClient(job.url);
  if (!client) { snprintf(job.error, sizeof(job.error), "out of memory"); return; }
  HTTPClient http;
  String body;

  int code = getJson(http, *client, job.url + "/api/builds/incomplete", job.token, body);
  if (code != HTTP_CODE_OK) {
    if (code == 401 || code == 403) snprintf(job.error, sizeof(job.error), "token refused (HTTP %d)", code);
    else setError(job, "builds", code);
    return;
  }
  if (!ciParseIncomplete(body.c_str(), body.length(), job.snapshot)) {
    snprintf(job.error, sizeof(job.error), "builds: unexpected reply");
    return;
  }
  CiSnapshot& s = job.snapshot;

  // How far each running build is. A failed detail request leaves the build
  // on the list with what the first reply said.
  for (uint8_t i = 0; i < s.runningCount; ++i) {
    CiBuild& b = s.running[i];
    code = getJson(http, *client, job.url + "/api/repos/" + b.slug + "/builds/" + String(b.number), job.token, body);
    if (code == HTTP_CODE_OK) ciParseDetail(body.c_str(), body.length(), b);
  }

  // What became of the builds that left the list.
  for (uint8_t i = 0; i < job.knownCount; ++i) {
    CiBuild b = job.known[i];
    bool listed = false;
    for (uint8_t k = 0; k < s.runningCount; ++k) if (s.running[k].id == b.id) listed = true;
    if (listed) continue;
    code = getJson(http, *client, job.url + "/api/repos/" + b.slug + "/builds/" + String(b.number), job.token, body);
    if (code != HTTP_CODE_OK || !ciParseDetail(body.c_str(), body.length(), b)) {
      // No answer: keep it as it was and ask again on the next poll.
      if (s.runningCount < kCiMaxBuilds) s.running[s.runningCount++] = job.known[i];
      continue;
    }
    if (b.status == CI_SUCCESS || b.status == CI_FAILED) {
      if (s.finishedCount < kCiMaxBuilds) s.finished[s.finishedCount++] = b;
    } else if (s.runningCount < kCiMaxBuilds) {
      s.running[s.runningCount++] = b;   // the list was a moment behind
    }
  }
  job.ok = true;
}

void poll(Job& job) {
  pollBuilds(job);     // its connection is closed before the picture is fetched
  if (!job.ok) return;
  CiSnapshot& s = job.snapshot;

  // One missing picture per poll. It is fetched before the result goes back,
  // so a build seen for the first time comes up with its picture; that costs
  // a few seconds once per person, after which the picture is in flash.
  for (uint8_t i = 0; i < s.runningCount && !job.avatarUrl.length(); ++i) {
    const char* url = s.running[i].avatar;
    if (!*url) continue;
    const uint32_t hash = urlHash(url);
    bool bad = false;
    for (uint8_t k = 0; k < job.badCount; ++k) if (job.badAvatars[k] == hash) bad = true;
    if (!bad && !LittleFS.exists(avatarPath(hash))) job.avatarUrl = url;
  }
  if (job.avatarUrl.length()) {
    if (fetchAvatar(job.avatarUrl)) job.avatarStored = true;
    else job.avatarFailed = true;
  }
}

void pollTask(void* arg) {
  Job* job = static_cast<Job*>(arg);
  if (job->avatarOnly) {
    if (fetchAvatar(job->avatarUrl)) job->avatarStored = true;
    else job->avatarFailed = true;
  } else {
    poll(*job);
  }
  g_result.store(job);
  vTaskDelete(nullptr);
}

// ---- pictures in RAM (main loop only) --------------------------------------
uint16_t* g_big = nullptr;
uint32_t  g_bigHash = 0;
struct Thumb { uint32_t hash; bool used; uint16_t px[CI_AVATAR_SMALL * CI_AVATAR_SMALL]; };
Thumb    g_thumbs[kCiMaxBuilds];
uint8_t  g_thumbNext = 0;
uint32_t g_avatarRevision = 0;

// ---- demo -------------------------------------------------------------------
bool     g_demo = false;
uint32_t g_demoStart = 0;
int      g_demoStage = -1;
char     g_demoAvatar[sizeof(CiBuild::avatar)] = {0};

CiBuild demoBuild(uint32_t id, const char* repo, const char* ref, const char* author, const char* step,
                  uint8_t done, uint8_t total, CiStatus status) {
  CiBuild b;
  b.id = id;
  b.number = id;
  b.status = status;
  b.stepsDone = done;
  b.stepsTotal = total;
  ciText(b.repo, repo);
  ciText(b.ref, ref);
  ciText(b.author, author);
  ciText(b.step, step);
  ciText(b.machine, "runner01");
  ciText(b.avatar, g_demoAvatar);
  return b;
}

void demoService() {
  const uint32_t now = millis(), t = now - g_demoStart;
  // When each stage starts, in milliseconds.
  static const uint32_t at[] = {0, 5000, 10000, 13000, 16000, 20000, 32000};
  int stage = 0;
  while (stage + 1 < int(sizeof(at) / sizeof(at[0])) && t >= at[stage + 1]) ++stage;
  if (stage == g_demoStage) return;
  g_demoStage = stage;
  CiSnapshot s;
  const uint32_t a = 0xD0000001u, b = 0xD0000002u;
  switch (stage) {
    case 0: s.running[s.runningCount++] = demoBuild(a, "shop-api", "v1.4.0", "alice", "clone", 0, 14, CI_RUNNING); break;
    case 1: s.running[s.runningCount++] = demoBuild(a, "shop-api", "v1.4.0", "alice", "docker-build", 3, 14, CI_RUNNING);
            s.running[s.runningCount++] = demoBuild(b, "website", "main", "bob", "clone", 0, 5, CI_RUNNING); break;
    case 2: s.running[s.runningCount++] = demoBuild(a, "shop-api", "v1.4.0", "alice", "push-image", 9, 14, CI_RUNNING);
            s.running[s.runningCount++] = demoBuild(b, "website", "main", "bob", "npm-test", 2, 5, CI_RUNNING); break;
    case 3: s.running[s.runningCount++] = demoBuild(a, "shop-api", "v1.4.0", "alice", "helm-deploy", 13, 14, CI_RUNNING);
            s.running[s.runningCount++] = demoBuild(b, "website", "main", "bob", "npm-test", 2, 5, CI_RUNNING); break;
    case 4: s.finished[s.finishedCount++] = demoBuild(a, "shop-api", "v1.4.0", "alice", "", 14, 14, CI_SUCCESS);
            s.running[s.runningCount++] = demoBuild(b, "website", "main", "bob", "npm-build", 3, 5, CI_RUNNING); break;
    case 5: s.finished[s.finishedCount++] = demoBuild(b, "website", "main", "bob", "npm-build", 3, 5, CI_FAILED); break;
    default:
      g_demo = false;
      g_tracker.clear();
      return;
  }
  g_tracker.apply(s, now);
}

void configureTracker(const Settings& s) {
  g_tracker.configure(uint32_t(s.ci.splashSec) * 1000, uint32_t(s.ci.failSec) * 1000, DEFAULT_CI_DONE_SEC * 1000);
}

}  // namespace

CiTracker& ciTracker() { return g_tracker; }

void ciBegin(const Settings& s) {
  configureTracker(s);
  // A download cut off by a reboot.
  LittleFS.remove(String(kDir) + "/avatar.tmp");
}

void ciApplySettings(const Settings& s) {
  configureTracker(s);
  ++g_generation;          // a poll still in the air is for the old settings
  g_tracker.clear();
  g_polled = g_ok = false;
  g_error[0] = 0;
  g_started = false;       // poll at once
  g_demo = false;
}

void ciStartDemo(const char* avatarUrl) {
  g_tracker.clear();
  g_demo = true;
  g_demoStart = millis();
  g_demoStage = -1;
  ciText(g_demoAvatar, avatarUrl ? avatarUrl : "");
  // The picture, unless it is in flash already. It arrives a few seconds into
  // the demo and the screen repaints with it.
  if (!g_demoAvatar[0] || g_busy || WiFi.status() != WL_CONNECTED) return;
  if (LittleFS.exists(avatarPath(urlHash(g_demoAvatar)))) return;
  Job* job = new (std::nothrow) Job();
  if (!job) return;
  job->avatarOnly = true;
  job->avatarUrl = g_demoAvatar;
  job->generation = g_generation;
  if (xTaskCreate(pollTask, "ci-poll", kTaskStack, job, 1, nullptr) != pdPASS) {
    delete job;
    return;
  }
  g_busy = true;
}

void ciService(const Settings& s) {
  // A poll that came back.
  if (Job* job = g_result.exchange(nullptr)) {
    g_busy = false;
    g_lastDoneMs = millis();
    if (job->generation == g_generation) {
      if (!job->avatarOnly) {
        g_polled = true;
        g_ok = job->ok;
        memcpy(g_error, job->error, sizeof(g_error));
        if (job->ok && !g_demo) g_tracker.apply(job->snapshot, millis());
      }
      if (job->avatarStored) ++g_avatarRevision;
      if (job->avatarFailed && g_badCount < sizeof(g_badAvatars) / sizeof(g_badAvatars[0]))
        g_badAvatars[g_badCount++] = urlHash(job->avatarUrl.c_str());
    }
    delete job;
  }

  if (g_demo) { demoService(); return; }

  if (!s.ci.enabled || !s.ci.url.length() || !s.ci.token.length()) {
    if (g_tracker.rows() || g_tracker.view(millis()) != CiTracker::IDLE) g_tracker.clear();
    return;
  }
  if (g_busy || WiFi.status() != WL_CONNECTED) return;
  if (g_started && millis() - g_lastStartMs < uint32_t(s.ci.pollSec) * 1000) return;
  if (ESP.getFreeHeap() < kMinFreeHeap) return;   // try again on the next turn

  Job* job = new (std::nothrow) Job();
  if (!job) return;
  job->url = s.ci.url;
  job->token = s.ci.token;
  job->generation = g_generation;
  job->knownCount = uint8_t(g_tracker.runningBuilds(job->known));
  job->badCount = g_badCount;
  memcpy(job->badAvatars, g_badAvatars, sizeof(g_badAvatars));
  if (xTaskCreate(pollTask, "ci-poll", kTaskStack, job, 1, nullptr) != pdPASS) {
    delete job;
    return;
  }
  g_busy = true;
  g_started = true;
  g_lastStartMs = millis();
}

CiState ciState(const Settings& s) {
  CiState st;
  st.enabled = s.ci.enabled;
  st.demo = g_demo;
  st.polled = g_polled;
  st.ok = g_ok;
  memcpy(st.error, g_error, sizeof(st.error));
  st.lastPollSec = g_polled ? (millis() - g_lastDoneMs) / 1000 : 0;
  st.running = uint8_t(g_tracker.rows());
  return st;
}

// ---- pictures ---------------------------------------------------------------
uint32_t ciAvatarRevision() { return g_avatarRevision; }

const uint16_t* ciAvatarBig(const char* url) {
  if (!url || !*url) return nullptr;
  const uint32_t hash = urlHash(url);
  if (g_big && g_bigHash == hash) return g_big;
  File f = LittleFS.open(avatarPath(hash), "r");
  const size_t bytes = CI_AVATAR_BIG * CI_AVATAR_BIG * 2;
  if (!f || f.size() != bytes) return nullptr;
  if (!g_big) g_big = static_cast<uint16_t*>(malloc(bytes));
  if (!g_big) return nullptr;
  if (f.read(reinterpret_cast<uint8_t*>(g_big), bytes) != bytes) { g_bigHash = 0; return nullptr; }
  g_bigHash = hash;
  return g_big;
}

const uint16_t* ciAvatarSmall(const char* url) {
  if (!url || !*url) return nullptr;
  const uint32_t hash = urlHash(url);
  for (Thumb& t : g_thumbs) if (t.used && t.hash == hash) return t.px;
  File f = LittleFS.open(avatarPath(hash), "r");
  if (!f || f.size() != size_t(CI_AVATAR_BIG * CI_AVATAR_BIG * 2)) return nullptr;
  Thumb& t = g_thumbs[g_thumbNext];
  g_thumbNext = uint8_t((g_thumbNext + 1) % kCiMaxBuilds);
  t.used = false;
  // Four source rows at a time, each block of 4x4 averaged into one pixel.
  const int k = CI_AVATAR_BIG / CI_AVATAR_SMALL;
  uint16_t rows[k * CI_AVATAR_BIG];
  for (int y = 0; y < CI_AVATAR_SMALL; ++y) {
    if (f.read(reinterpret_cast<uint8_t*>(rows), sizeof(rows)) != sizeof(rows)) return nullptr;
    for (int x = 0; x < CI_AVATAR_SMALL; ++x) {
      uint32_t r = 0, g = 0, b = 0;
      for (int dy = 0; dy < k; ++dy)
        for (int dx = 0; dx < k; ++dx) {
          const uint16_t px = rows[dy * CI_AVATAR_BIG + x * k + dx];
          r += px >> 11;
          g += (px >> 5) & 0x3F;
          b += px & 0x1F;
        }
      const int n = k * k;
      t.px[y * CI_AVATAR_SMALL + x] = uint16_t(((r / n) << 11) | ((g / n) << 5) | (b / n));
    }
  }
  t.hash = hash;
  t.used = true;
  return t.px;
}

void ciAvatarRelease() {
  free(g_big);
  g_big = nullptr;
  g_bigHash = 0;
  for (Thumb& t : g_thumbs) t.used = false;
}
#endif
