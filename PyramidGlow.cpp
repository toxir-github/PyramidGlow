// PyramidGlow.cpp - OpenFX wrapper around glow_core.h (CPU, float RGBA).
#include <cstring>
#include <memory>
#include <vector>

#include "glow_core.h"
#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

using namespace OFX;

class PyramidGlowPlugin : public ImageEffect {
 protected:
  Clip *dstClip_, *srcClip_;
  DoubleParam *threshold_, *knee_, *intensity_, *radius_, *decay_, *gamma_;
  RGBParam* tint_;
  BooleanParam *glowAlpha_, *glowOnly_;

 public:
  explicit PyramidGlowPlugin(OfxImageEffectHandle handle) : ImageEffect(handle) {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    threshold_ = fetchDoubleParam("threshold");
    knee_ = fetchDoubleParam("knee");
    intensity_ = fetchDoubleParam("intensity");
    radius_ = fetchDoubleParam("radius");
    decay_ = fetchDoubleParam("decay");
    gamma_ = fetchDoubleParam("gamma");
    tint_ = fetchRGBParam("tint");
    glowAlpha_ = fetchBooleanParam("glowAlpha");
    glowOnly_ = fetchBooleanParam("glowOnly");
  }

  void render(const RenderArguments& args) override {
    if (dstClip_->getPixelDepth() != eBitDepthFloat ||
        dstClip_->getPixelComponents() != ePixelComponentRGBA)
      throwSuiteStatusException(kOfxStatErrUnsupported);

    std::unique_ptr<Image> dst(dstClip_->fetchImage(args.time));
    std::unique_ptr<Image> src(srcClip_->fetchImage(args.time));
    if (!dst) throwSuiteStatusException(kOfxStatFailed);

    const OfxRectI rw = args.renderWindow;
    if (!src) {  // no input: output transparent black
      for (int y = rw.y1; y < rw.y2; ++y) {
        float* d = (float*)dst->getPixelAddress(rw.x1, y);
        std::memset(d, 0, sizeof(float) * 4 * (rw.x2 - rw.x1));
      }
      return;
    }

    dglow::Params p;
    const double t = args.time;
    p.threshold = (float)threshold_->getValueAtTime(t);
    p.knee = (float)knee_->getValueAtTime(t);
    p.intensity = (float)intensity_->getValueAtTime(t);
    p.radius = (float)radius_->getValueAtTime(t);
    p.decay = (float)decay_->getValueAtTime(t);
    p.gamma = (float)gamma_->getValueAtTime(t);
    double r, g, b;
    tint_->getValueAtTime(t, r, g, b);
    p.tint[0] = (float)r; p.tint[1] = (float)g; p.tint[2] = (float)b;
    p.glowAlpha = glowAlpha_->getValueAtTime(t);
    p.glowOnly = glowOnly_->getValueAtTime(t);

    // Copy source (its full bounds) into a contiguous buffer.
    const OfxRectI sb = src->getBounds();
    const int w = sb.x2 - sb.x1, h = sb.y2 - sb.y1;
    if (w <= 0 || h <= 0) return;
    std::vector<float> in((size_t)w * h * 4), out((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
      std::memcpy(&in[(size_t)y * w * 4], src->getPixelAddress(sb.x1, sb.y1 + y),
                  sizeof(float) * 4 * w);

    dglow::process(in.data(), out.data(), w, h, p, (float)args.renderScale.x);

    // Write the render window back out.
    for (int y = rw.y1; y < rw.y2; ++y) {
      if (abort()) return;
      float* d = (float*)dst->getPixelAddress(rw.x1, y);
      for (int x = rw.x1; x < rw.x2; ++x, d += 4) {
        if (x >= sb.x1 && x < sb.x2 && y >= sb.y1 && y < sb.y2) {
          std::memcpy(d, &out[((size_t)(y - sb.y1) * w + (x - sb.x1)) * 4], sizeof(float) * 4);
        } else {
          d[0] = d[1] = d[2] = d[3] = 0.f;
        }
      }
    }
  }

  bool isIdentity(const IsIdentityArguments& args, Clip*& identityClip,
                  double& identityTime) override {
    if (intensity_->getValueAtTime(args.time) == 0.0 && !glowOnly_->getValueAtTime(args.time)) {
      identityClip = srcClip_;
      identityTime = args.time;
      return true;
    }
    return false;
  }
};

mDeclarePluginFactory(PyramidGlowFactory, {}, {});

static DoubleParamDescriptor* defDouble(ImageEffectDescriptor& desc, PageParamDescriptor* page,
                                        const char* name, const char* label, const char* hint,
                                        double def, double lo, double hi, double dlo, double dhi,
                                        double inc) {
  DoubleParamDescriptor* p = desc.defineDoubleParam(name);
  p->setLabels(label, label, label);
  p->setScriptName(name);
  p->setHint(hint);
  p->setDefault(def);
  p->setRange(lo, hi);
  p->setDisplayRange(dlo, dhi);
  p->setIncrement(inc);
  page->addChild(*p);
  return p;
}

void PyramidGlowFactory::describe(ImageEffectDescriptor& desc) {
  desc.setLabels("Pyramid Glow", "Pyramid Glow", "Pyramid Glow");
  desc.setPluginGrouping("Glow Lab");
  desc.setPluginDescription("Multi-scale glow built from a blur pyramid.");
  desc.addSupportedContext(eContextFilter);
  desc.addSupportedContext(eContextGeneral);
  desc.addSupportedBitDepth(eBitDepthFloat);
  desc.setSingleInstance(false);
  desc.setHostFrameThreading(false);
  desc.setSupportsMultiResolution(true);
  desc.setSupportsTiles(false);  // glow needs the whole frame
  desc.setTemporalClipAccess(false);
  desc.setRenderTwiceAlways(false);
  desc.setSupportsMultipleClipPARs(false);
}

void PyramidGlowFactory::describeInContext(ImageEffectDescriptor& desc, ContextEnum) {
  ClipDescriptor* src = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
  src->addSupportedComponent(ePixelComponentRGBA);
  src->setTemporalClipAccess(false);
  src->setSupportsTiles(false);
  src->setIsMask(false);

  ClipDescriptor* dst = desc.defineClip(kOfxImageEffectOutputClipName);
  dst->addSupportedComponent(ePixelComponentRGBA);
  dst->setSupportsTiles(false);

  PageParamDescriptor* page = desc.definePageParam("Controls");
  defDouble(desc, page, "threshold", "Threshold", "Brightness above which pixels start to glow (linear light).",
            0.8, 0.0, 100.0, 0.0, 2.0, 0.01);
  defDouble(desc, page, "knee", "Soft Knee", "0 = hard cutoff, 1 = very gradual.", 0.5, 0.0, 1.0,
            0.0, 1.0, 0.01);
  defDouble(desc, page, "intensity", "Intensity", "Glow gain.", 1.0, 0.0, 100.0, 0.0, 5.0, 0.05);
  defDouble(desc, page, "radius", "Radius", "Maximum glow spread in pixels (at full resolution).",
            64.0, 0.0, 4096.0, 0.0, 512.0, 1.0);
  defDouble(desc, page, "decay", "Falloff", "0 = wide flat glow, higher = bright tight core with a soft tail.",
            0.35, 0.0, 3.0, 0.0, 1.5, 0.01);
  defDouble(desc, page, "gamma", "Input Gamma",
            "1.0 if the image is already linear. 2.2-2.4 if gamma encoded (glow is added in linear light).",
            1.0, 0.2, 5.0, 0.5, 3.0, 0.1);

  RGBParamDescriptor* tint = desc.defineRGBParam("tint");
  tint->setLabels("Tint", "Tint", "Tint");
  tint->setScriptName("tint");
  tint->setDefault(1.0, 1.0, 1.0);
  page->addChild(*tint);

  BooleanParamDescriptor* ga = desc.defineBooleanParam("glowAlpha");
  ga->setLabels("Glow Affects Alpha", "Glow Affects Alpha", "Glow Affects Alpha");
  ga->setDefault(true);
  page->addChild(*ga);

  BooleanParamDescriptor* go = desc.defineBooleanParam("glowOnly");
  go->setLabels("Glow Only", "Glow Only", "Glow Only");
  go->setDefault(false);
  page->addChild(*go);
}

ImageEffect* PyramidGlowFactory::createInstance(OfxImageEffectHandle handle, ContextEnum) {
  return new PyramidGlowPlugin(handle);
}

namespace OFX {
namespace Plugin {
void getPluginIDs(OFX::PluginFactoryArray& ids) {
  static PyramidGlowFactory p("com.glowlab.PyramidGlow", 1, 0);
  ids.push_back(&p);
}
}  // namespace Plugin
}  // namespace OFX
