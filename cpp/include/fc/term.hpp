// The interactive shell engine behind the Terminal view.
//
// A virtual filesystem is generated from the site data and a small set of
// commands runs over it. Output is structured, never HTML: each line is a list
// of spans {t: text, k: kind, h: href, c: command}, so the page renders it with
// textContent and user input can never become markup.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "fc/json.hpp"

namespace fc {

// The latest report the page has for a station (a snapshot, or a labeled sample).
struct MetarSource {
  std::string raw;
  std::string observedAt;  // ISO time, for display
  bool sample = false;
};

struct TermOptions {
  std::vector<std::string> modes;                // enabled views the `mode` command may open, e.g. "finance"
  std::map<std::string, MetarSource> metars;     // by ICAO code; empty means `metar` says "coming soon"
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

  // Output of `stats`. `raw` is the /stats payload (or null when the bot did not answer). `history` is the
  // optional /stats/history payload for the sparkline; without it the sparkline is a labeled illustration.
  // `padToBlock` pads the output to a fixed number of lines so it can replace the placeholder in place.
  Json statsOutput(const Json& raw, const std::string& trackingSince, const Json& history = Json(),
                   bool padToBlock = false) const;

  // The same block with [+x.x%] placeholders, for the first screen.
  Json statsPlaceholder(const std::string& trackingSince, bool padToBlock = false) const;

  // Every file as [{path, content}], for the <noscript> copy of the same facts.
  Json files() const;

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace fc
