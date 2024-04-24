#include "LtcGen.h"
#include "EffectEditor.h"
#include "LoadEffects.h"

#include "ltc.h"

#include <math.h>
#include <iostream>

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
     mfps, msrate
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

size_t generateLTC(double fps, double srate, ltcsnd_sample_t** data_buffer) {
   FILE* file;
   file = fopen("test.data", "wb"); 
   double length = 2; // in seconds
	double sample_rate = srate;
	char *filename;

    double ltc_fps = fps;

	int vframe_cnt;
	int vframe_last;

	int total = 0;
	ltcsnd_sample_t *buf;

	LTCEncoder *encoder;
	SMPTETimecode st;

	/* start encoding at this timecode */
	const char timezone[6] = "+0100";
	strcpy(st.timezone, timezone);
	st.years =  8;
	st.months = 12;
	st.days =   31;

	st.hours = 23;
	st.mins = 59;
	st.secs = 59;
	st.frame = 0; 
	encoder = ltc_encoder_create(sample_rate, ltc_fps,
		ltc_fps==25?LTC_TV_625_50:LTC_TV_525_60, LTC_USE_DATE);
	ltc_encoder_set_timecode(encoder, &st);

    vframe_cnt = 0;
    vframe_last = length * ltc_fps;
    size_t encoder_buf_size = ltc_encoder_get_buffersize(encoder);
    size_t data_buffer_size = encoder_buf_size * vframe_last;
    *data_buffer = (ltcsnd_sample_t*)malloc(data_buffer_size);


	while (vframe_cnt++ < vframe_last) {
        ltc_encoder_encode_frame(encoder);
        int len = ltc_encoder_get_bufferptr(encoder, &buf, 1);
        if (len > 0) {
            fwrite(buf, sizeof(ltcsnd_sample_t), len, file);
            memcpy(*data_buffer+total, buf, len);
            total+=len;
        }
        ltc_encoder_inc_timecode(encoder);
	}
    fclose(file);
	ltc_encoder_free(encoder);
   // END LTC STUFF
   return data_buffer_size;
}

size_t EffectLtcGen::ProcessBlock(EffectSettings&,
   const float* const*, float* const* outbuf, size_t size)
{
   if (!timecodeCalculated) {
       double ltc_fps;
       switch (fps) {
          default:
          case k24:
             ltc_fps = 24;
             break;
          case k25:
             ltc_fps = 25;
             break;
          case k30:
             ltc_fps = 30;
             break;
          case k29_97:
             ltc_fps = 30;
             break;
       }
       data_buffer_size = generateLTC(ltc_fps, srate, &data_buffer);
       timecodeCalculated = true;
   }
   
   // BEGIN LTC MAGIC
   
   float* output_buffer = outbuf[0];

   for (decltype(size) i = 0; i < size; i++)
   {
      if (buf_pos < data_buffer_size) {
          output_buffer[i] = static_cast<float>(data_buffer[buf_pos]) / 255.0 - 0.5;
          buf_pos++;
      } else {
          output_buffer[i] = 0;
      }
   }

   return size;

}

std::unique_ptr<EffectEditor> EffectLtcGen::PopulateOrExchange(
   ShuttleGui& S, EffectInstance&, EffectSettingsAccess& access,
   const EffectOutputs*)
{
   mUIParent = S.GetParent();
   wxTextCtrl *t;

   wxASSERT(nTypes == WXSIZEOF(kFpsStrings));

   S.StartMultiColumn(2, wxCENTER);
   {
      S.Validator<wxGenericValidator>(&fps)
         .AddChoice(XXO("&Frames Per Second:"), Msgids(kFpsStrings, nTypes));

      S.Validator<FloatingPointValidator<double>>(
            6, &srate, NumValidatorStyle::NO_TRAILING_ZEROES, msrate.min, msrate.max)
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
