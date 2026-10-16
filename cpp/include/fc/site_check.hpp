// Validation of the one site data file (src/data/site.json).
#pragma once

#include <string>
#include <vector>

#include "fc/json.hpp"

namespace fc {

struct Issue {
  enum class Severity { Error, Warning };
  Severity severity;
  std::string path;     // e.g. "projects[1].status"
  std::string message;
};

// Checks structure, enumerations, coordinate ranges, honesty rules for the bot
// section, and scans every string for `forbiddenTerms` (case-insensitive).
// Strings that start with "TODO" are reported as warnings so placeholders
// cannot be forgotten.
std::vector<Issue> checkSite(const Json& site, const std::vector<std::string>& forbiddenTerms);

Json issuesToJson(const std::vector<Issue>& issues);

}  // namespace fc
