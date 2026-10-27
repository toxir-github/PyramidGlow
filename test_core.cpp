// test_core.cpp - run the glow algorithm on a raw float RGBA file.
// usage: test_core in.f32 w h out.f32 [threshold intensity radius decay]
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "glow_core.h"

int main(int argc, char** argv) {
  if (argc < 5) { std::fprintf(stderr, "usage: %s in.f32 w h out.f32 [thr int rad decay]\n", argv[0]); return 1; }
  const int w = std::atoi(argv[2]), h = std::atoi(argv[3]);
  std::vector<float> in((size_t)w * h * 4), out((size_t)w * h * 4);
  FILE* f = std::fopen(argv[1], "rb");
  if (!f || std::fread(in.data(), sizeof(float), in.size(), f) != in.size()) { std::fprintf(stderr, "read failed\n"); return 1; }
  std::fclose(f);
  dglow::Params p;
  if (argc > 5) p.threshold = (float)std::atof(argv[5]);
  if (argc > 6) p.intensity = (float)std::atof(argv[6]);
  if (argc > 7) p.radius = (float)std::atof(argv[7]);
  if (argc > 8) p.decay = (float)std::atof(argv[8]);
  dglow::process(in.data(), out.data(), w, h, p, 1.0f);
  f = std::fopen(argv[4], "wb");
  std::fwrite(out.data(), sizeof(float), out.size(), f);
  std::fclose(f);
  return 0;
}
