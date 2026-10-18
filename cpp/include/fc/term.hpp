// The interactive shell engine behind the Terminal view.
//
// A virtual filesystem is generated from the site data and a small set of
// commands runs over it. Output is structured, never HTML: each line is a list
// of spans {t: text, k: kind, h: href, c: command}, so the page renders it with
// textContent and user input can never become markup.
#pragma once

#include <string>
#include <vector>

#include "fc/json.hpp"

namespace fc {

struct TermOptions {
  std::vector<std::string> modes;  // enabled views the `mode` command may open, e.g. "finance"
  bool metarReady = false;         // flips on when the Aviation decoder is wired in
};

class Term {
 public:
  Term(const Json& site, TermOptions options);
  ~Term();
  Term(const Term&) = delete;
  Term& operator=(const Term&) = delete;

  // Runs one command line. `cwd` is "~" or "~/projects"; `history` includes the
  // line being run; `now` is Unix seconds (passed in so tests are deterministic).
  // Returns {lines, actions, cwd}. Actions: {type:"clear"}, {type:"navigate",url},
  // {type:"stats"}.
  Json exec(const std::string& line, const std::string& cwd, const std::vector<std::string>& history,
            long long now) const;

  // Tab completion for the text before the cursor. Returns {start, options}.
  Json complete(const std::string& line, const std::string& cwd) const;

  // Output of `stats` once the page has fetched (or failed to fetch) the bot's /stats.
  // `raw` is the payload or null on failure.
  Json statsOutput(const Json& raw, const std::string& trackingSince) const;

  // Every file as [{path, content}], for the <noscript> copy of the same facts.
  Json files() const;

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace fc
