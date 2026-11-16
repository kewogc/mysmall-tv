// The Drone replies as the build screen reads them (src/features/ci/CiParse.h),
// against replies saved from a Drone 2.28 server with the names replaced.
#include "CiParse.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
using namespace smalltv;

static std::string file(const char* name) {
  std::ifstream in(std::string("tests/ci/fixtures/") + name);
  assert(in);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

int main() {
  {  // The list of running builds.
    const std::string json = file("incomplete-running.json");
    CiSnapshot s;
    assert(ciParseIncomplete(json.data(), json.size(), s));
    assert(s.runningCount == 1 && s.moreRunning == 0);
    const CiBuild& b = s.running[0];
    assert(b.id == 28 && b.number == 15 && b.status == CI_RUNNING && b.started == 1794802549u);
    assert(!strcmp(b.repo, "shop") && !strcmp(b.slug, "acme/shop"));
    assert(!strcmp(b.ref, "16.11.2026-11.15"));          // refs/tags/ taken off
    assert(!strcmp(b.author, "dev"));
    assert(!strcmp(b.avatar, "https://git.example.com/avatars/0123456789abcdef0123456789abcdef"));
  }
  {  // Nothing runs; more builds than rows; not a list.
    CiSnapshot s;
    assert(ciParseIncomplete("[]", 2, s) && s.runningCount == 0);
    std::string many = "[";
    for (int i = 0; i < 6; ++i)
      many += std::string(i ? "," : "") + "{\"name\":\"r\",\"slug\":\"a/r\",\"build\":{\"id\":" + std::to_string(i + 1) +
              ",\"number\":1,\"status\":\"pending\",\"ref\":\"refs/heads/main\",\"target\":\"main\"}}";
    many += "]";
    assert(ciParseIncomplete(many.data(), many.size(), s));
    assert(s.runningCount == kCiMaxBuilds && s.moreRunning == 2);
    assert(s.running[0].status == CI_PENDING && !strcmp(s.running[0].ref, "main") && s.running[0].started == 0);
    assert(!ciParseIncomplete("{\"message\":\"Unauthorized\"}", 26, s));
    assert(!ciParseIncomplete("<html>", 6, s));
  }
  {  // A running build: two stages, the first steps running.
    const std::string json = file("build-running.json");
    CiBuild b;
    assert(ciParseDetail(json.data(), json.size(), b));
    assert(b.status == CI_RUNNING && b.stepsTotal == 19 && b.stepsDone == 0);
    assert(!strcmp(b.step, "clone") && !strcmp(b.machine, "runner01"));
  }
  {  // A build that ended well.
    const std::string json = file("build-done.json");
    CiBuild b;
    assert(ciParseDetail(json.data(), json.size(), b));
    assert(b.status == CI_SUCCESS && b.stepsTotal == 4 && b.stepsDone == 4 && b.step[0] == 0);
  }
  {  // A failure names the step; the running step comes from the longer stage.
    const char* failed = R"({"status":"failure","started":5,"stages":[{"name":"Deploy","status":"failure","machine":"m1","steps":[)"
                         R"({"name":"clone","status":"success"},{"name":"docker-build","status":"failure"},{"name":"deploy","status":"skipped"}]}]})";
    CiBuild b;
    assert(ciParseDetail(failed, strlen(failed), b));
    assert(b.status == CI_FAILED && !strcmp(b.step, "docker-build") && b.stepsDone == 3 && b.stepsTotal == 3 && b.started == 5);
    const char* two = R"({"status":"running","stages":[{"name":"audit","status":"running","machine":"","steps":[{"name":"scan","status":"running"}]},)"
                      R"({"name":"Deploy","status":"running","machine":"m2","steps":[{"name":"clone","status":"success"},{"name":"build","status":"running"},{"name":"push","status":"pending"}]}]})";
    assert(ciParseDetail(two, strlen(two), b));
    assert(b.status == CI_RUNNING && !strcmp(b.step, "build") && !strcmp(b.machine, "m2") && b.stepsDone == 1 && b.stepsTotal == 4);
    // A stage that failed before it had steps.
    const char* early = R"({"status":"error","stages":[{"name":"Deploy","status":"error","machine":"","steps":[]}]})";
    assert(ciParseDetail(early, strlen(early), b) && b.status == CI_FAILED && !strcmp(b.step, "Deploy"));
    assert(!ciParseDetail("{\"message\":\"Not Found\"}", 23, b));
  }
  assert(ciStatus("killed") == CI_FAILED && ciStatus("blocked") == CI_PENDING && ciStatus(nullptr) == CI_PENDING);
  puts("ci parse tests passed");
}
