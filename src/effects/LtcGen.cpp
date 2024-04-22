#include "LtcGen.h"
#include "EffectEditor.h"
#include "LoadEffects.h"

#include <math.h>

#include <wx/choice.h>
#include <wx/textctrl.h>
#include <wx/valgen.h>

#include "Prefs.h"
#include "ShuttleGui.h"
#include "../widgets/valnum.h"
#include "../widgets/NumericTextCtrl.h"

const EnumValueSymbol EffectLtcGen::kFpsStrings[nTypes] =
{
   
   { XC("24", "fps") },
   
   { XC("25", "fps") },
   
   { XC("30", "fps") },

   { XC("29.97", "fps") }
};

const EffectParameterMethods& EffectLtcGen::Parameters() const
{
   static CapturedParameters<EffectLtcGen,
      fps, frate
   > parameters;
   return parameters;
}

const ComponentInterfaceSymbol EffectLtcGen::Symbol
{ XO("LTC waveform") };

namespace { BuiltinEffectsModule::Registration< EffectLtcGen > reg; }

EffectLtcGen::EffectLtcGen()
{
   Parameters().Reset(*this);

   SetLinearEffectFlag(true);
}

EffectLtcGen::~EffectLtcGen()
{
}

ComponentInterfaceSymbol EffectLtcGen::GetSymbol() const
{
   return Symbol;
}

TranslatableString EffectLtcGen::GetDescription() const
{
   return XO("Generates LTC waveform from parameters");
}

ManualPageID EffectLtcGen::ManualPage() const
{
   return L"LTC";
}

EffectType EffectLtcGen::GetType() const
{
   return EffectTypeGenerate;
}

unsigned EffectLtcGen::GetAudioOutCount() const
{
   return 1;
}

bool EffectLtcGen::ProcessInitialize(EffectSettings&,
   double sampleRate, ChannelNames)
{
   mSampleRate = sampleRate;
   return true;
}

size_t EffectLtcGen::ProcessBlock(EffectSettings&,
   const float* const*, float* const* outbuf, size_t size)
{
   float* buffer = outbuf[0];

   for (decltype(size) i = 0; i < size; i++)
   {
      buffer[i] = 0;
   }

   return size;

}

std::unique_ptr<EffectEditor> EffectLtcGen::PopulateOrExchange(
   ShuttleGui& S, EffectInstance&, EffectSettingsAccess& access,
   const EffectOutputs*)
{
   mUIParent = S.GetParent();

   wxASSERT(nTypes == WXSIZEOF(kFpsStrings));

   S.StartMultiColumn(2, wxCENTER);
   {
      S.Validator<wxGenericValidator>(&mfps)
         .AddChoice(XXO("&Frames Per Second:"), Msgids(kFpsStrings, nTypes));

      S
         .Validator<FloatingPointValidator<double>>(
            6, &mfrate, NumValidatorStyle::NO_TRAILING_ZEROES, frate.min, frate.max)
         .AddTextBox(XXO("&Frequency Rate:"), L"", 12);

      S.AddPrompt(XXO("&Duration:"));
      auto& extra = access.Get().extra;
      mLtcGenDurationT = safenew
         NumericTextCtrl(FormatterContext::SampleRateContext(mProjectRate),
            S.GetParent(), wxID_ANY,
            NumericConverterType_TIME(),
            extra.GetDurationFormat(),
            extra.GetDuration(),
            NumericTextCtrl::Options{}
      .AutoPos(true));
      S.Name(XO("Duration"))
         .Position(wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxALL)
         .AddWindow(mLtcGenDurationT);
   }
   S.EndMultiColumn();
   return nullptr;
}

bool EffectLtcGen::TransferDataToWindow(const EffectSettings& settings)
{
   if (!mUIParent->TransferDataToWindow())
   {
      return false;
   }

   mLtcGenDurationT->SetValue(settings.extra.GetDuration());
   return true;
}

bool EffectLtcGen::TransferDataFromWindow(EffectSettings& settings)
{
   if (!mUIParent->Validate() || !mUIParent->TransferDataFromWindow())
   {
      return false;
   }

   settings.extra.SetDuration(mLtcGenDurationT->GetValue());
   return true;
}
