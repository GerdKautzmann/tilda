// modplay.h - a small ProTracker (.mod) player. No libraries needed.
//
// Handles the common 4, 6 and 8 channel module formats ("M.K.", "4CHN", "6CHN", "8CHN", ...) and the
// effects almost every module uses: arpeggio, portamento, tone portamento, vibrato, sample offset,
// volume slide, set volume, position jump, pattern break and speed/tempo. Everything else is ignored.
// The module loops forever. Output is mono floats, added to the buffer you pass in.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class ModPlayer {
 public:
  bool loaded() const { return !pat_.empty(); }
  const std::string& title() const { return title_; }

  bool load(const std::string& path, std::string& err) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { err = "cannot open " + path; return false; }
    std::vector<uint8_t> b;
    uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    fclose(f);
    return parse(b, path, err);
  }

  // Adds `n` mono samples of music to out[], scaled by gain.
  void render(float* out, int n, int rate, float gain) {
    if (!loaded()) return;
    const float g = gain * 4.0f / (float)ch_.size();
    for (int i = 0; i < n; i++) {
      if (--tickLeft_ <= 0) {
        tick(rate);
      }
      float s = 0;
      for (Chan& c : ch_) {
        if (c.smp < 0 || c.period <= 0) continue;
        const Sample& sm = smp_[c.smp];
        if (sm.data.empty()) continue;
        int p = (int)c.pos;
        if (p >= (int)sm.data.size()) continue;
        s += (sm.data[p] / 128.0f) * (c.vol / 64.0f);
        c.pos += 3546894.6f / (std::max(1.f, c.curPeriod)) / (float)rate;
        if (sm.loopLen > 2) {
          float end = (float)(sm.loopStart + sm.loopLen);
          if (c.pos >= end) c.pos = (float)sm.loopStart + std::fmod(c.pos - (float)sm.loopStart, (float)sm.loopLen);
        } else if (c.pos >= (float)sm.data.size()) {
          c.smp = -1;
        }
      }
      out[i] += s * g;
    }
  }

 private:
  struct Sample {
    std::vector<int8_t> data;
    int vol = 0, loopStart = 0, loopLen = 0;
  };
  struct Cell { int period, smp, eff, par; };
  struct Chan {
    int smp = -1, vol = 0;
    float pos = 0, period = 0, curPeriod = 0, target = 0;
    int portaSpeed = 0, vibSpeed = 0, vibDepth = 0, vibPos = 0, lastSample = 0;
    int effect = 0, param = 0, cutAt = -1;
  };

  std::string title_;
  Sample smp_[31];
  std::vector<std::vector<Cell>> pat_;  // [pattern][row * channels + channel]
  std::vector<int> order_;
  std::vector<Chan> ch_;
  int row_ = 0, ord_ = 0, tick_ = 0, speed_ = 6, tempo_ = 125, tickLeft_ = 0;
  int jumpOrd_ = -1, breakRow_ = -1;

  static int be16(const uint8_t* p) { return (p[0] << 8) | p[1]; }

  bool parse(const std::vector<uint8_t>& b, const std::string& path, std::string& err) {
    if (b.size() < 1084) { err = path + ": too small to be a module"; return false; }
    std::string sig((const char*)&b[1080], 4);
    int chans = 0;
    if (sig == "M.K." || sig == "M!K!" || sig == "FLT4" || sig == "4CHN") chans = 4;
    else if (sig == "6CHN") chans = 6;
    else if (sig == "8CHN" || sig == "OCTA" || sig == "CD81") chans = 8;
    else if (sig[2] == 'C' && sig[3] == 'H' && isdigit((unsigned char)sig[0]) && isdigit((unsigned char)sig[1]))
      chans = (sig[0] - '0') * 10 + (sig[1] - '0');
    if (chans < 1 || chans > 32) { err = path + ": not a 4/6/8 channel ProTracker module (signature '" + sig + "')"; return false; }

    title_.assign((const char*)&b[0], 20);
    title_ = title_.c_str();
    size_t off = 20;
    int lens[31];
    for (int i = 0; i < 31; i++, off += 30) {
      lens[i] = be16(&b[off + 22]) * 2;
      smp_[i].vol = std::min(64, (int)b[off + 25]);
      smp_[i].loopStart = be16(&b[off + 26]) * 2;
      smp_[i].loopLen = be16(&b[off + 28]) * 2;
    }
    int songLen = std::max(1, std::min(128, (int)b[950]));
    int maxPat = 0;
    order_.clear();
    for (int i = 0; i < 128; i++) maxPat = std::max(maxPat, (int)b[952 + i]);
    for (int i = 0; i < songLen; i++) order_.push_back(b[952 + i]);

    size_t patBytes = (size_t)64 * chans * 4;
    size_t need = 1084 + patBytes * (maxPat + 1);
    if (b.size() < need) { err = path + ": file is cut off (pattern data missing)"; return false; }
    pat_.assign(maxPat + 1, {});
    for (int p = 0; p <= maxPat; p++) {
      pat_[p].resize((size_t)64 * chans);
      const uint8_t* q = &b[1084 + patBytes * p];
      for (int i = 0; i < 64 * chans; i++, q += 4) {
        Cell& c = pat_[p][i];
        c.smp = (q[0] & 0xF0) | (q[2] >> 4);
        c.period = ((q[0] & 0x0F) << 8) | q[1];
        c.eff = q[2] & 0x0F;
        c.par = q[3];
      }
    }
    size_t pos = need;
    for (int i = 0; i < 31; i++) {
      size_t len = std::min((size_t)lens[i], b.size() > pos ? b.size() - pos : 0);
      smp_[i].data.assign((const int8_t*)&b[pos], (const int8_t*)&b[pos] + len);
      pos += len;
      if (smp_[i].loopStart >= (int)len) smp_[i].loopLen = 0;
      else if (smp_[i].loopStart + smp_[i].loopLen > (int)len) smp_[i].loopLen = (int)len - smp_[i].loopStart;
    }
    ch_.assign(chans, Chan());
    row_ = -1; ord_ = 0; tick_ = 0; speed_ = 6; tempo_ = 125; tickLeft_ = 0;
    jumpOrd_ = breakRow_ = -1;
    tick_ = speed_ - 1;  // the first tick() call starts row 0
    return true;
  }

  static float semis(float period, int n) { return period * std::pow(2.0f, -n / 12.0f); }

  void tick(int rate) {
    tickLeft_ += (int)(rate * 2.5f / (float)tempo_);
    if (++tick_ >= speed_) {
      tick_ = 0;
      if (jumpOrd_ >= 0 || breakRow_ >= 0) {
        if (jumpOrd_ >= 0) ord_ = jumpOrd_; else ord_++;
        row_ = std::max(0, breakRow_);
        jumpOrd_ = breakRow_ = -1;
      } else if (++row_ >= 64) {
        row_ = 0; ord_++;
      }
      if (ord_ >= (int)order_.size()) ord_ = 0;
      if (row_ >= 64) row_ = 0;
      startRow();
    } else {
      for (Chan& c : ch_) effectTick(c);
    }
  }

  void startRow() {
    const std::vector<Cell>& pat = pat_[std::min((size_t)order_[ord_], pat_.size() - 1)];
    for (size_t k = 0; k < ch_.size(); k++) {
      Chan& c = ch_[k];
      const Cell& cell = pat[(size_t)row_ * ch_.size() + k];
      c.effect = cell.eff; c.param = cell.par; c.cutAt = -1;
      if (cell.smp > 0 && cell.smp <= 31) { c.lastSample = cell.smp - 1; c.vol = smp_[c.lastSample].vol; }
      bool toneP = cell.eff == 3 || cell.eff == 5;
      if (cell.period > 0) {
        if (toneP) c.target = (float)cell.period;
        else {
          c.period = c.curPeriod = (float)cell.period;
          c.smp = c.lastSample; c.pos = 0; c.vibPos = 0;
          if (cell.eff == 9 && cell.par) c.pos = (float)cell.par * 256;
        }
      }
      switch (cell.eff) {
        case 3: if (cell.par) c.portaSpeed = cell.par; break;
        case 4:
          if (cell.par >> 4) c.vibSpeed = cell.par >> 4;
          if (cell.par & 15) c.vibDepth = cell.par & 15;
          break;
        case 0xB: jumpOrd_ = cell.par; break;
        case 0xC: c.vol = std::min(64, cell.par); break;
        case 0xD: breakRow_ = std::min(63, (cell.par >> 4) * 10 + (cell.par & 15)); break;
        case 0xE:
          if ((cell.par >> 4) == 0xC) c.cutAt = cell.par & 15;
          break;
        case 0xF:
          if (cell.par == 0) break;
          if (cell.par < 32) speed_ = cell.par; else tempo_ = cell.par;
          break;
      }
      if (c.cutAt == 0) c.vol = 0;
      c.curPeriod = c.period;
      if (cell.eff == 0 && cell.par) c.curPeriod = c.period;  // arpeggio starts on the base note
    }
  }

  void effectTick(Chan& c) {
    switch (c.effect) {
      case 0:
        if (c.param) {
          int t = tick_ % 3;
          c.curPeriod = t == 0 ? c.period : semis(c.period, t == 1 ? c.param >> 4 : c.param & 15);
        }
        break;
      case 1: c.period = std::max(54.f, c.period - c.param); c.curPeriod = c.period; break;
      case 2: c.period = std::min(1712.f, c.period + c.param); c.curPeriod = c.period; break;
      case 3: case 5:
        if (c.target > 0) {
          if (c.period > c.target) c.period = std::max(c.target, c.period - c.portaSpeed);
          else if (c.period < c.target) c.period = std::min(c.target, c.period + c.portaSpeed);
        }
        c.curPeriod = c.period;
        if (c.effect == 5) slide(c);
        break;
      case 4: case 6:
        c.vibPos += c.vibSpeed;
        c.curPeriod = c.period + std::sin((float)c.vibPos * 6.2831853f / 64.f) * c.vibDepth * 2.f;
        if (c.effect == 6) slide(c);
        break;
      case 0xA: slide(c); break;
      case 0xE:
        if ((c.param >> 4) == 0xC && tick_ == (c.param & 15)) c.vol = 0;
        break;
    }
  }

  static void slide(Chan& c) {
    int up = c.param >> 4, down = c.param & 15;
    if (up) c.vol = std::min(64, c.vol + up);
    else c.vol = std::max(0, c.vol - down);
  }
};
