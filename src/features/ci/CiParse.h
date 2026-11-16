// CiParse.h — the Drone CI replies the build screen reads.
//
//   GET /api/builds/incomplete              every running build, one item each
//   GET /api/repos/{slug}/builds/{number}   one build with its stages and steps
//
// Checked against a Drone 2.28 server. Plain C++ over ArduinoJson, so
// tests/ci parses saved replies on the desktop.
#pragma once
#include <ArduinoJson.h>
#include "CiTracker.h"

namespace smalltv {

template <size_t N> inline void ciText(char (&dst)[N], const char* src) {
  size_t n = 0;
  if (src) for (; src[n] && n + 1 < N; ++n) dst[n] = src[n];
  dst[n] = 0;
}

inline CiStatus ciStatus(const char* s) {
  if (!s) return CI_PENDING;
  if (!strcmp(s, "running")) return CI_RUNNING;
  if (!strcmp(s, "success") || !strcmp(s, "skipped")) return CI_SUCCESS;
  if (!strcmp(s, "failure") || !strcmp(s, "error") || !strcmp(s, "killed") || !strcmp(s, "declined")) return CI_FAILED;
  return CI_PENDING;   // pending, blocked, waiting_on_dependencies
}

// "refs/tags/1.2" -> "1.2", "refs/heads/main" -> "main"; else the target branch.
inline const char* ciShortRef(const char* ref, const char* target) {
  if (ref) {
    if (!strncmp(ref, "refs/tags/", 10)) return ref + 10;
    if (!strncmp(ref, "refs/heads/", 11)) return ref + 11;
  }
  return (target && *target) ? target : (ref ? ref : "");
}

// The running builds. The reply carries each build's whole commit message, so
// a filter keeps only what the screen uses. False when the text is not the
// expected JSON array.
inline bool ciParseIncomplete(const char* json, size_t length, CiSnapshot& out) {
  JsonDocument filter;
  JsonObject f = filter[0].to<JsonObject>();
  f["name"] = true;
  f["slug"] = true;
  JsonObject fb = f["build"].to<JsonObject>();
  for (const char* key : {"id", "number", "status", "ref", "target", "started", "author_login", "author_avatar", "sender"}) fb[key] = true;

  JsonDocument doc;
  if (deserializeJson(doc, json, length, DeserializationOption::Filter(filter))) return false;
  if (!doc.is<JsonArrayConst>()) return false;
  out.runningCount = out.moreRunning = 0;
  for (JsonObjectConst item : doc.as<JsonArrayConst>()) {
    JsonObjectConst b = item["build"];
    if (b.isNull()) continue;
    if (out.runningCount == kCiMaxBuilds) {
      if (out.moreRunning < 255) ++out.moreRunning;
      continue;
    }
    CiBuild& c = out.running[out.runningCount++];
    c = CiBuild();
    c.id      = b["id"] | 0u;
    c.number  = b["number"] | 0u;
    c.started = b["started"] | 0u;
    c.status  = ciStatus(b["status"] | "pending");
    if (c.status != CI_RUNNING) c.status = CI_PENDING;   // on this list a build runs or waits
    ciText(c.repo, item["name"] | "");
    ciText(c.slug, item["slug"] | "");
    ciText(c.ref, ciShortRef(b["ref"] | "", b["target"] | ""));
    const char* who = b["author_login"] | "";
    ciText(c.author, *who ? who : (b["sender"] | ""));
    ciText(c.avatar, b["author_avatar"] | "");
  }
  return true;
}

// One build in detail: its status, how far its steps are, the step that runs
// (or the one that failed) and the runner. Fills those fields of `b` and
// leaves the rest. False when the text is not the expected JSON object.
inline bool ciParseDetail(const char* json, size_t length, CiBuild& b) {
  JsonDocument filter;
  filter["status"] = true;
  filter["started"] = true;
  JsonObject fs = filter["stages"][0].to<JsonObject>();
  fs["name"] = true;
  fs["status"] = true;
  fs["machine"] = true;
  JsonObject fp = fs["steps"][0].to<JsonObject>();
  fp["name"] = true;
  fp["status"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, json, length, DeserializationOption::Filter(filter))) return false;
  if (!doc["status"].is<const char*>()) return false;
  b.status = ciStatus(doc["status"]);
  if (uint32_t started = doc["started"] | 0u) b.started = started;

  unsigned total = 0, done = 0, bestSteps = 0;
  const char* running = nullptr;   // the running step of the stage with the most steps
  const char* failed = nullptr;
  b.machine[0] = 0;
  for (JsonObjectConst stage : doc["stages"].as<JsonArrayConst>()) {
    if (!b.machine[0]) ciText(b.machine, stage["machine"] | "");
    unsigned steps = 0;
    const char* runsHere = nullptr;
    for (JsonObjectConst step : stage["steps"].as<JsonArrayConst>()) {
      ++steps;
      const char* st = step["status"] | "pending";
      const CiStatus s = ciStatus(st);
      if (s == CI_SUCCESS || s == CI_FAILED) ++done;
      if (s == CI_RUNNING && !runsHere) runsHere = step["name"] | "";
      if (s == CI_FAILED && !failed) failed = step["name"] | "";
    }
    // A stage that failed before it had steps (a bad pipeline file, no runner).
    if (!steps && ciStatus(stage["status"] | "") == CI_FAILED && !failed) failed = stage["name"] | "";
    if (runsHere && steps > bestSteps) { bestSteps = steps; running = runsHere; }
    total += steps;
  }
  b.stepsTotal = uint8_t(total > 255 ? 255 : total);
  b.stepsDone  = uint8_t(done > 255 ? 255 : done);
  if (b.status == CI_FAILED) ciText(b.step, failed ? failed : "");
  else                       ciText(b.step, running ? running : "");
  return true;
}

}  // namespace smalltv
