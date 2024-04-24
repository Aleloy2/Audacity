#include "LtcGen.h"
#include "EffectEditor.h"
#include "LoadEffects.h"

#include "ltc.h"

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

size_t EffectLtcGen::ProcessBlock(EffectSettings&,
   const float* const*, float* const* outbuf, size_t size)
{
   // BEGIN LTC MAGIC
   FILE* file;
   file = fopen("test.wav", "wb"); 
   double length = 2; // in seconds
	double sample_rate = srate;
	char *filename;

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
	encoder = ltc_encoder_create(sample_rate, fps,
		fps==25?LTC_TV_625_50:LTC_TV_525_60, LTC_USE_DATE);
	ltc_encoder_set_timecode(encoder, &st);

	buf = (ltcsnd_sample_t*) calloc(ltc_encoder_get_buffersize(encoder), sizeof(ltcsnd_sample_t));
	if (!buf) {
			return -1;
	}

    vframe_cnt = 0;
    vframe_last = length * fps;

	while (vframe_cnt++ < vframe_last) {
	/* encode and write each of the 80 LTC frame bits (10 bytes) */
		int byte_cnt;
		for (byte_cnt = 0 ; byte_cnt < 10 ; byte_cnt++) {
			ltc_encoder_encode_byte(encoder, byte_cnt, 1.0);
			int len = ltc_encoder_copy_buffer(encoder, buf);
            printf("TEST\n");
			if (len > 0) {
                printf("HI\n");
				fwrite(buf, sizeof(ltcsnd_sample_t), len, file);
				total+=len;
			}
		}
        fclose(file);
		ltc_encoder_inc_timecode(encoder);
	}
	ltc_encoder_free(encoder);
   // END LTC STUFF
   
   float* output_buffer = outbuf[0];

   for (decltype(size) i = 0; i < size; i++)
   {
      output_buffer[i] = 0;
   }
    // memcpy(output_buffer, buf, size);

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
