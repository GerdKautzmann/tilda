// story.h - the story script: dialogs, quest objectives and triggers live in assets/story.txt, not in the code.
//
// This header only reads and evaluates the script. What the commands and variables MEAN is decided by
// the game (main.cpp). See the comments at the top of assets/story.txt for the format.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct StCond { std::string var; char op; int val; };   // op: '=', '!' (not equal), '<', '>', 'l' (<=), 'g' (>=)
struct StSay { std::string who, text; };                // who: "$" = the person talked to, "" = narrator
struct StAct { std::string cmd, arg; };
struct StVariant {                                      // one conversation; a block with several of them cycles through them
  std::vector<StSay> say;
  std::vector<StAct> acts;                              // run when the conversation ends
  std::string text;                                     // objective blocks: the text for the status page
};
struct StBlock {
  std::vector<StCond> conds;                            // all of them must hold; none = always ("else")
  std::vector<StVariant> variants;
  int count = 0;                                        // how often this block has been used (picks the variant)
  bool fired = false;                                   // trigger blocks fire once
  int line = 0;
};
struct Story {
  std::map<std::string, std::vector<StBlock>> talk, examine;
  std::vector<StBlock> objective, trigger;
  void resetCounters() {
    for (auto* m : {&talk, &examine})
      for (auto& kv : *m) for (StBlock& b : kv.second) b.count = 0;
    for (auto* v : {&objective, &trigger})
      for (StBlock& b : *v) { b.count = 0; b.fired = false; }
  }
};

typedef std::function<int(const std::string&)> StVarFn;

inline std::string stTrim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// "quest>=5" -> {quest, 'g', 5}
inline bool stParseCond(const std::string& tok, StCond& c) {
  static const struct { const char* s; char op; } ops[] = {{">=", 'g'}, {"<=", 'l'}, {"!=", '!'}, {"=", '='}, {"<", '<'}, {">", '>'}};
  for (const auto& o : ops) {
    size_t p = tok.find(o.s);
    if (p == std::string::npos || p == 0) continue;
    c.var = tok.substr(0, p);
    c.op = o.op;
    std::string v = tok.substr(p + strlen(o.s));
    if (v.empty()) return false;
    c.val = atoi(v.c_str());
    return true;
  }
  return false;
}

inline bool stHolds(const std::vector<StCond>& conds, const StVarFn& var) {
  for (const StCond& c : conds) {
    int v = var(c.var);
    bool ok = c.op == '=' ? v == c.val : c.op == '!' ? v != c.val : c.op == '<' ? v < c.val : c.op == '>' ? v > c.val
            : c.op == 'l' ? v <= c.val : v >= c.val;
    if (!ok) return false;
  }
  return true;
}

// "{kills} slimes" -> "3 slimes"
inline std::string stExpand(const std::string& s, const StVarFn& var) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '{') {
      size_t e = s.find('}', i);
      if (e != std::string::npos) {
        out += std::to_string(var(s.substr(i + 1, e - i - 1)));
        i = e;
        continue;
      }
    }
    out += s[i];
  }
  return out;
}

inline bool loadStory(const std::string& path, Story& st, std::string& err) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) { err = "cannot open " + path; return false; }
  Story out;
  std::vector<StBlock>* sect = nullptr;   // the block list we are adding to
  StBlock* blk = nullptr;
  StVariant* var = nullptr;
  bool isObjective = false;
  int ln = 0, errors = 0;
  char buf[2048];
  auto bad = [&](const std::string& msg) {
    if (errors++ < 5) err += path + ":" + std::to_string(ln) + ": " + msg + "\n";
  };
  while (fgets(buf, sizeof buf, f)) {
    ln++;
    std::string line = stTrim(buf);
    if (line.empty() || line[0] == '#') continue;
    size_t sp = line.find_first_of(" \t");
    std::string kw = line.substr(0, sp), rest = sp == std::string::npos ? "" : stTrim(line.substr(sp + 1));
    if (kw == "talk" || kw == "examine") {
      if (rest.empty()) { bad(kw + " needs a name"); continue; }
      sect = kw == "talk" ? &out.talk[rest] : &out.examine[rest];
      blk = nullptr; var = nullptr; isObjective = false;
    } else if (kw == "objective") {
      sect = &out.objective; blk = nullptr; var = nullptr; isObjective = true;
    } else if (kw == "trigger") {
      sect = &out.trigger; blk = nullptr; var = nullptr; isObjective = false;
    } else if (kw == "when" || kw == "else") {
      if (!sect) { bad("'" + kw + "' before talk / examine / objective / trigger"); continue; }
      sect->push_back(StBlock());
      blk = &sect->back();
      blk->line = ln;
      blk->variants.push_back(StVariant());
      var = &blk->variants.back();
      if (kw == "when") {
        size_t p = 0;
        while (p < rest.size()) {
          size_t q = rest.find_first_of(" \t", p);
          std::string tok = rest.substr(p, q == std::string::npos ? std::string::npos : q - p);
          if (!tok.empty()) {
            StCond c;
            if (stParseCond(tok, c)) blk->conds.push_back(c);
            else bad("cannot read the condition '" + tok + "' (use e.g. quest>=5)");
          }
          if (q == std::string::npos) break;
          p = q + 1;
        }
      }
    } else if (kw == "---") {
      if (!blk) { bad("'---' outside a block"); continue; }
      blk->variants.push_back(StVariant());
      var = &blk->variants.back();
    } else if (kw == "say") {
      if (!var) { bad("'say' outside a block"); continue; }
      size_t bar = rest.find('|');
      if (bar == std::string::npos) { bad("say needs 'Name | text' (leave the name empty for the narrator)"); continue; }
      var->say.push_back({stTrim(rest.substr(0, bar)), stTrim(rest.substr(bar + 1))});
    } else if (kw == "do") {
      if (!var) { bad("'do' outside a block"); continue; }
      size_t s2 = rest.find_first_of(" \t");
      var->acts.push_back({rest.substr(0, s2), s2 == std::string::npos ? "" : stTrim(rest.substr(s2 + 1))});
    } else if (kw == "text") {
      if (!var || !isObjective) { bad("'text' only belongs in an objective block"); continue; }
      var->text = rest;
    } else {
      bad("unknown command '" + kw + "'");
    }
  }
  fclose(f);
  if (errors) return false;
  st = std::move(out);
  return true;
}

inline std::string storyFileFor(const std::string& worldPath) {
  size_t sl = worldPath.find_last_of("/\\");
  return (sl == std::string::npos ? std::string() : worldPath.substr(0, sl + 1)) + "story.txt";
}
