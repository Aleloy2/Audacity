#ifndef __AUDACITY_EFFECT_LTCGEN__
#define __AUDACITY_EFFECT_LTCGEN__

#include "StatefulPerTrackEffect.h"
#include "ShuttleAutomation.h"
#include <wx/weakref.h>

class NumericTextCtrl;
class ShuttleGui;

class EffectLtcGen : public StatefulPerTrackEffect
{
public:
   static inline EffectLtcGen*
   FetchParameters(EffectLtcGen& e, EffectSettings&) { return &e; }
   static const ComponentInterfaceSymbol Symbol;

   EffectLtcGen();
   virtual ~EffectLtcGen();

   // ComponentInterface implementation

   ComponentInterfaceSymbol GetSymbol() const override;
   TranslatableString GetDescription() const override;
   ManualPageID ManualPage() const override;

   // EffectDefinitionInterface implementation

   EffectType GetType() const override;

   unsigned GetAudioOutCount() const override;
   bool ProcessInitialize(EffectSettings& settings, double sampleRate,
      ChannelNames chanMap) override;
   size_t ProcessBlock(EffectSettings& settings,
      const float* const* inBlock, float* const* outBlock, size_t blockLen)
      override;

   // Effect implementation

   std::unique_ptr<EffectEditor> PopulateOrExchange(
      ShuttleGui& S, EffectInstance& instance,
      EffectSettingsAccess& access, const EffectOutputs* pOutputs) override;
   bool TransferDataToWindow(const EffectSettings& settings) override;
   bool TransferDataFromWindow(EffectSettings& settings) override;

private:
   // EffectToneGen implementation

   wxWeakRef<wxWindow> mUIParent{};

   double mSampleRate{};
   int fps;
   double srate;

   NumericTextCtrl* mLtcGenDurationT;

   const EffectParameterMethods& Parameters() const override;

   enum kfps
   {
      k24,
      k25,
      k30,
      k29_97,
      nTypes
   };
   static const EnumValueSymbol kFpsStrings[nTypes];

   static constexpr EnumParameter mfps{ &EffectLtcGen::fps,
   L"Frames Per Second",       k24,  0,    nTypes - 1, 1, kFpsStrings, nTypes };
   static constexpr EffectParameter msrate{ &EffectLtcGen::srate,
   L"Frame Rate",   44100,     1,     96000,           1 };

};

#endif
