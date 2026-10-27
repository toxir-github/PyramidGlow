// glow_core.h - host-independent glow algorithm (blur pyramid).
// Input/output: interleaved float RGBA, contiguous, w*h*4 floats.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace dglow {

struct Params {
  float threshold = 0.8f;  // highlight cutoff, in linear light
  float knee = 0.5f;       // soft knee width (0 = hard cutoff, 1 = very soft)
  float intensity = 1.0f;  // glow gain
  float radius = 64.0f;    // max glow spread in pixels (full resolution)
  float decay = 0.35f;     // per-level exponential falloff: 0 = flat/wide, higher = tight core
  float tint[3] = {1.f, 1.f, 1.f};
  float gamma = 1.0f;      // input encoding gamma: 1 = already linear, 2.2 = gamma-encoded
  bool glowAlpha = true;   // let the glow add to alpha
  bool glowOnly = false;   // output only the glow (for debugging / compositing)
};

// Interleaved RGB float image.
struct Buf {
  int w = 0, h = 0;
  std::vector<float> d;
  Buf() {}
  Buf(int w_, int h_) : w(w_), h(h_), d((size_t)w_ * h_ * 3, 0.f) {}
  float* px(int x, int y) { return &d[((size_t)y * w + x) * 3]; }
  const float* px(int x, int y) const { return &d[((size_t)y * w + x) * 3]; }
};

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Separable 5-tap binomial blur [1 4 6 4 1]/16 with edge clamp.
inline void blur5(Buf& b) {
  if (b.w < 1 || b.h < 1) return;
  static const float k[5] = {1.f / 16, 4.f / 16, 6.f / 16, 4.f / 16, 1.f / 16};
  Buf t(b.w, b.h);
#pragma omp parallel for
  for (int y = 0; y < b.h; ++y)
    for (int x = 0; x < b.w; ++x) {
      float acc[3] = {0, 0, 0};
      for (int i = -2; i <= 2; ++i) {
        const float* p = b.px(clampi(x + i, 0, b.w - 1), y);
        for (int c = 0; c < 3; ++c) acc[c] += k[i + 2] * p[c];
      }
      float* o = t.px(x, y);
      for (int c = 0; c < 3; ++c) o[c] = acc[c];
    }
#pragma omp parallel for
  for (int y = 0; y < b.h; ++y)
    for (int x = 0; x < b.w; ++x) {
      float acc[3] = {0, 0, 0};
      for (int i = -2; i <= 2; ++i) {
        const float* p = t.px(x, clampi(y + i, 0, b.h - 1));
        for (int c = 0; c < 3; ++c) acc[c] += k[i + 2] * p[c];
      }
      float* o = b.px(x, y);
      for (int c = 0; c < 3; ++c) o[c] = acc[c];
    }
}

// 2x2 box downsample (edge clamp for odd sizes).
inline Buf down2(const Buf& s) {
  Buf o(std::max(1, (s.w + 1) / 2), std::max(1, (s.h + 1) / 2));
#pragma omp parallel for
  for (int y = 0; y < o.h; ++y)
    for (int x = 0; x < o.w; ++x) {
      const int x0 = clampi(2 * x, 0, s.w - 1), x1 = clampi(2 * x + 1, 0, s.w - 1);
      const int y0 = clampi(2 * y, 0, s.h - 1), y1 = clampi(2 * y + 1, 0, s.h - 1);
      const float *a = s.px(x0, y0), *b = s.px(x1, y0), *c = s.px(x0, y1), *e = s.px(x1, y1);
      float* d = o.px(x, y);
      for (int k = 0; k < 3; ++k) d[k] = 0.25f * (a[k] + b[k] + c[k] + e[k]);
    }
  return o;
}

// dst += bilinear_upsample(src) (src is smaller; dst has target size)
inline void addUpsampled(const Buf& s, Buf& dst) {
  const float sx = (float)s.w / (float)dst.w, sy = (float)s.h / (float)dst.h;
#pragma omp parallel for
  for (int y = 0; y < dst.h; ++y) {
    const float fy = (y + 0.5f) * sy - 0.5f;
    const int y0 = (int)std::floor(fy);
    const float ty = fy - y0;
    const int ya = clampi(y0, 0, s.h - 1), yb = clampi(y0 + 1, 0, s.h - 1);
    for (int x = 0; x < dst.w; ++x) {
      const float fx = (x + 0.5f) * sx - 0.5f;
      const int x0 = (int)std::floor(fx);
      const float tx = fx - x0;
      const int xa = clampi(x0, 0, s.w - 1), xb = clampi(x0 + 1, 0, s.w - 1);
      const float *p00 = s.px(xa, ya), *p10 = s.px(xb, ya), *p01 = s.px(xa, yb), *p11 = s.px(xb, yb);
      float* o = dst.px(x, y);
      for (int c = 0; c < 3; ++c) {
        const float top = p00[c] + (p10[c] - p00[c]) * tx;
        const float bot = p01[c] + (p11[c] - p01[c]) * tx;
        o[c] += top + (bot - top) * ty;
      }
    }
  }
}

inline float toLinear(float v, float g) {
  if (g == 1.0f) return v;
  return std::copysign(std::pow(std::fabs(v), g), v);
}
inline float fromLinear(float v, float g) {
  if (g == 1.0f) return v;
  return std::copysign(std::pow(std::fabs(v), 1.0f / g), v);
}

// Soft-knee highlight extraction. Scales RGB so hue is preserved.
inline void prefilter(float* rgb, float threshold, float knee) {
  const float br = std::max(rgb[0], std::max(rgb[1], rgb[2]));
  const float kw = threshold * knee + 1e-5f;
  float soft = std::min(std::max(br - threshold + kw, 0.f), 2.f * kw);
  soft = soft * soft / (4.f * kw + 1e-5f);
  const float contrib = std::max(br - threshold, soft);
  const float s = br > 1e-6f ? std::max(contrib, 0.f) / br : 0.f;
  rgb[0] *= s; rgb[1] *= s; rgb[2] *= s;
}

// Builds the glow layer (RGB, linear, normalized) from linear RGB highlights.
inline Buf buildGlow(Buf highlights, const Params& p, float renderScale) {
  const float radius = std::max(p.radius * renderScale, 0.f);
  std::vector<Buf> lvl;
  blur5(highlights);
  lvl.push_back(std::move(highlights));
  while ((int)lvl.size() < 12 && std::min(lvl.back().w, lvl.back().h) > 2) {
    Buf n = down2(lvl.back());
    blur5(n);
    lvl.push_back(std::move(n));
  }
  const int n = (int)lvl.size();
  // Level i has an effective spread of about 2^i px. Levels beyond log2(radius) fade out.
  const float Lr = std::log2(std::max(radius, 1.0f));
  std::vector<float> wgt(n);
  float sum = 0.f;
  for (int i = 0; i < n; ++i) {
    const float cutoff = std::min(std::max(Lr - (float)i + 1.f, 0.f), 1.f);
    wgt[i] = std::exp(-p.decay * (float)i) * cutoff;
    sum += wgt[i];
  }
  Buf acc(lvl[0].w, lvl[0].h);
  if (sum <= 0.f) return acc;  // radius 0: no glow
  Buf cur = lvl[n - 1];
  for (float& v : cur.d) v *= wgt[n - 1] / sum;
  for (int i = n - 2; i >= 0; --i) {
    Buf nxt(lvl[i].w, lvl[i].h);
    for (size_t k = 0; k < nxt.d.size(); ++k) nxt.d[k] = lvl[i].d[k] * (wgt[i] / sum);
    addUpsampled(cur, nxt);
    cur = std::move(nxt);
  }
  return cur;
}

// Main entry. in/out: interleaved RGBA float, w*h*4. out may not alias in.
inline void process(const float* in, float* out, int w, int h, const Params& p, float renderScale) {
  Buf hi(w, h);
  const size_t N = (size_t)w * h;
#pragma omp parallel for
  for (long long i = 0; i < (long long)N; ++i) {
    float* o = &hi.d[(size_t)i * 3];
    for (int c = 0; c < 3; ++c) o[c] = toLinear(in[(size_t)i * 4 + c], p.gamma);
    prefilter(o, p.threshold, p.knee);
  }
  const Buf glow = buildGlow(std::move(hi), p, renderScale);
#pragma omp parallel for
  for (long long i = 0; i < (long long)N; ++i) {
    const float* g = &glow.d[(size_t)i * 3];
    float add[3], gmax = 0.f;
    for (int c = 0; c < 3; ++c) {
      add[c] = g[c] * p.intensity * p.tint[c];
      gmax = std::max(gmax, add[c]);
    }
    const float a = in[(size_t)i * 4 + 3];
    float* o = out + (size_t)i * 4;
    for (int c = 0; c < 3; ++c) {
      const float base = p.glowOnly ? 0.f : toLinear(in[(size_t)i * 4 + c], p.gamma);
      o[c] = fromLinear(base + add[c], p.gamma);
    }
    float ao = p.glowOnly ? 0.f : a;
    if (p.glowAlpha) ao = ao + (1.f - ao) * std::min(std::max(gmax, 0.f), 1.f);
    o[3] = ao;
  }
}

}  // namespace dglow
