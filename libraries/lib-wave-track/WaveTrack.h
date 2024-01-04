/**********************************************************************

  Audacity: A Digital Audio Editor

  WaveTrack.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_WAVETRACK__
#define __AUDACITY_WAVETRACK__

#include "ClipInterface.h"
#include "PlaybackDirection.h"
#include "Prefs.h"
#include "SampleCount.h"
#include "SampleFormat.h"
#include "SampleTrack.h"
#include "WideSampleSequence.h"

#include <vector>
#include <functional>
#include <wx/thread.h>
#include <wx/longlong.h>

class AudacityProject;
class BlockArray;

namespace BasicUI{ class ProgressDialog; }

class SampleBlockFactory;
using SampleBlockFactoryPtr = std::shared_ptr<SampleBlockFactory>;

class TimeWarper;

class ClipInterface;
class Sequence;
class WaveClip;
class AudioSegmentSampleView;

//! Clips are held by shared_ptr, not for sharing, but to allow weak_ptr
using WaveClipHolder = std::shared_ptr<WaveClip>;
using WaveClipConstHolder = std::shared_ptr<const WaveClip>;
using WaveClipHolders = std::vector<WaveClipHolder>;
using WaveClipConstHolders = std::vector<WaveClipConstHolder>;

using ClipConstHolders = std::vector<std::shared_ptr<const ClipInterface>>;

// Temporary arrays of mere pointers
using WaveClipPointers = std::vector < WaveClip* >;
using WaveClipConstPointers = std::vector < const WaveClip* >;

using ChannelSampleView = std::vector<AudioSegmentSampleView>;
using ChannelGroupSampleView = std::vector<ChannelSampleView>;

using TimeInterval = std::pair<double, double>;
using ProgressReporter = std::function<void(double)>;

//
// Tolerance for merging wave tracks (in seconds)
//
#define WAVETRACK_MERGE_POINT_TOLERANCE 0.01

class Envelope;
class WaveTrack;

class WAVE_TRACK_API WaveChannelInterval final
   : public ChannelInterval
   , public ClipTimes
{
public:
   //! Assume lifetime of this object nests in those of arguments
   WaveChannelInterval(WaveClip &wideClip, WaveClip &narrowClip, size_t iChannel
   )  : mWideClip{ wideClip }
      , mNarrowClip{ narrowClip }
      , miChannel{ iChannel }
   {}
   ~WaveChannelInterval() override;

   friend bool operator ==(
      const WaveChannelInterval &x, const WaveChannelInterval &y
   ) {
      return &x.mWideClip == &y.mWideClip &&
         &x.mNarrowClip == &y.mNarrowClip &&
         x.miChannel == y.miChannel;
   }
   friend bool operator !=(
      const WaveChannelInterval &x, const WaveChannelInterval &y
   ) {
      return !(x == y);
   }

   const WaveClip &GetWideClip() const { return mWideClip; }
   WaveClip &GetClip() { return mNarrowClip; }
   const WaveClip &GetClip() const { return mNarrowClip; }
   Envelope &GetEnvelope();
   const Envelope &GetEnvelope() const;
   size_t GetChannelIndex() const { return miChannel; }

   bool Intersects(double t0, double t1) const;
   double Start() const;
   double End() const;

   /*!
    * @brief Request interval samples within [t0, t1). `t0` and `t1` are
    * truncated to the interval start and end. Stretching influences the number
    * of samples fitting into [t0, t1), i.e., half as many for twice as large a
    * stretch ratio, due to a larger spacing of the raw samples. The actual
    * number of samples available from the returned view is queried through
    * `AudioSegmentSampleView::GetSampleCount()`.
    *
    * @pre samples in [t0, t1) can be counted with `size_t`
    */
   AudioSegmentSampleView
   GetSampleView(double t0, double t1, bool mayThrow) const;

   sampleCount GetVisibleSampleCount() const override;
   int GetRate() const override;
   double GetPlayStartTime() const override;
   double GetPlayEndTime() const override;
   double GetPlayDuration() const;

   /*!
    * @brief  t ∈ [...)
    */
   bool WithinPlayRegion(double t) const;

   // TimeToSamples and SamplesToTime take clip stretch ratio into account.
   // Use them to convert time / sample offsets.
   sampleCount TimeToSamples(double time) const override;
   double SamplesToTime(sampleCount s) const noexcept;

   double GetStretchRatio() const override;

   double GetTrimLeft() const;
   double GetTrimRight() const;

   bool GetSamples(samplePtr buffer, sampleFormat format,
      sampleCount start, size_t len, bool mayThrow = true) const;

   AudioSegmentSampleView GetSampleView(
      sampleCount start, size_t length, bool mayThrow) const;

   const Sequence &GetSequence() const;

   constSamplePtr GetAppendBuffer() const;
   size_t GetAppendBufferLen() const;

   int GetColourIndex() const;

   BlockArray *GetSequenceBlockArray();

   /*!
    Getting high-level data for one channel for screen display and clipping
    calculations and Contrast
    */
   std::pair<float, float> GetMinMax(double t0, double t1, bool mayThrow) const;

   /*!
    @copydoc GetMinMax
    */
   float GetRMS(double t0, double t1, bool mayThrow) const;

   //! Real start time of the clip, quantized to raw sample rate (track's rate)
   sampleCount GetPlayStartSample() const;

   //! Real end time of the clip, quantized to raw sample rate (track's rate)
   sampleCount GetPlayEndSample() const;

   /*!
    @param start relative to clip play start sample
    */
   void SetSamples(constSamplePtr buffer, sampleFormat format,
      sampleCount start, size_t len,
      sampleFormat effectiveFormat /*!<
         Make the effective format of the data at least the minumum of this
         value and `format`.  (Maybe wider, if merging with preexistent data.)
         If the data are later narrowed from stored format, but not narrower
         than the effective, then no dithering will occur.
      */
   );

private:
   WaveClip &GetNarrowClip() { return mNarrowClip; }
   const WaveClip &GetNarrowClip() const { return mNarrowClip; }

   WaveClip &mWideClip;
   WaveClip &mNarrowClip;
   const size_t miChannel;
};

class WAVE_TRACK_API WaveChannel
   : public Channel
   // TODO wide wave tracks -- remove "virtual"
   , public virtual WideSampleSequence
{
public:
   ~WaveChannel() override;

   inline WaveTrack &GetTrack();
   inline const WaveTrack &GetTrack() const;

   auto GetInterval(size_t iInterval) { return
      ::Channel::GetInterval<WaveChannelInterval>(iInterval); }
   auto GetInterval(size_t iInterval) const { return
      ::Channel::GetInterval<const WaveChannelInterval>(iInterval); }

   auto Intervals() { return ::Channel::Intervals<WaveChannelInterval>(); }
   auto Intervals() const {
      return ::Channel::Intervals<const WaveChannelInterval>(); }

   using WideSampleSequence::GetFloats;

   //! "narrow" overload fetches from the unique channel
   bool GetFloats(float *buffer, sampleCount start, size_t len,
      fillFormat fill = FillFormat::fillZero, bool mayThrow = true,
      sampleCount * pNumWithinClips = nullptr) const
   {
      constexpr auto backwards = false;
      return GetFloats(
         0, 1, &buffer, start, len, backwards, fill, mayThrow, pNumWithinClips);
   }

   /*!
    * @brief Request channel samples within [t0, t1), not knowing in advance how
    * many this will be.
    *
    * @details The stretching of intersecting intervals influences the number of
    * samples fitting into [t0, t1), i.e., half as many for twice as large a
    * stretch ratio, due to a larger spacing of the raw samples.
    *
    * @pre samples in [t0, t1) can be counted with `size_t`
    */
   ChannelSampleView GetSampleView(double t0, double t1, bool mayThrow) const;

   //! Random-access assignment of a range of samples
   [[nodiscard]] bool Set(constSamplePtr buffer, sampleFormat format,
      sampleCount start, size_t len,
      sampleFormat effectiveFormat = widestSampleFormat /*!<
         Make the effective format of the data at least the minumum of this
         value and `format`.  (Maybe wider, if merging with preexistent data.)
         If the data are later narrowed from stored format, but not narrower
         than the effective, then no dithering will occur.
      */
   );

   //! Random-access assignment of a range of samples
   [[nodiscard]] bool SetFloats(const float *buffer,
      sampleCount start, size_t len,
      sampleFormat effectiveFormat = widestSampleFormat /*!<
         Make the effective format of the data at least the minumum of this
         value and `format`.  (Maybe wider, if merging with preexistent data.)
         If the data are later narrowed from stored format, but not narrower
         than the effective, then no dithering will occur.
      */
   )
   {
      return Set(reinterpret_cast<constSamplePtr>(buffer), floatSample,
         start, len, effectiveFormat);
   }

   bool AppendBuffer(constSamplePtr buffer, sampleFormat format, size_t len, unsigned stride, sampleFormat effectiveFormat);

   /*!
    If there is an existing WaveClip in the WaveTrack that owns the channel,
    then the data are appended to that clip. If there are no WaveClips in the
    track, then a new one is created.
    @return true if at least one complete block was created
    */
   bool Append(constSamplePtr buffer, sampleFormat format, size_t len);

   //! A hint for sizing of well aligned fetches
   inline size_t GetBestBlockSize(sampleCount t) const;
   //! A hint for sizing of well aligned fetches
   inline size_t GetIdealBlockSize();
   //! A hint for maximum returned by either of GetBestBlockSize,
   //! GetIdealBlockSize
   inline size_t GetMaxBlockSize() const;
};

class WAVE_TRACK_API WaveTrack final
   : public WritableSampleTrack
   // TODO wide wave tracks -- remove this base class
   , public WaveChannel
{
public:
   class Interval;
   using IntervalHolder = std::shared_ptr<Interval>;
   using IntervalHolders = std::vector<IntervalHolder>;
   using IntervalConstHolder = std::shared_ptr<const Interval>;
   using IntervalConstHolders = std::vector<IntervalConstHolder>;

   // Resolve ambiguous lookup
   using SampleTrack::GetFloats;

   /// \brief Structure to hold region of a wavetrack and a comparison function
   /// for sortability.
   struct Region
   {
      Region() : start(0), end(0) {}
      Region(double start_, double end_) : start(start_), end(end_) {}

      double start, end;

      //used for sorting
      bool operator < (const Region &b) const
      {
         return this->start < b.start;
      }
   };

   using Regions = std::vector < Region >;

   static wxString GetDefaultAudioTrackNamePreference();

   static double ProjectNyquistFrequency(const AudacityProject &project);

   //
   // Constructor / Destructor / Duplicator
   //

   // Construct and also build all attachments
   static WaveTrack *New( AudacityProject &project );

   WaveTrack(
      const SampleBlockFactoryPtr &pFactory, sampleFormat format, double rate);
   //! Copied only in WaveTrack::Clone() !
   WaveTrack(const WaveTrack &orig, ProtectedCreationArg&&);

   //! The width of every WaveClip in this track; for now always 1
   size_t GetWidth() const;

   //! May report more than one only when this is a leader track
   size_t NChannels() const override;

   auto GetChannel(size_t iChannel) {
      return this->ChannelGroup::GetChannel<WaveChannel>(iChannel); }
   auto GetChannel(size_t iChannel) const {
      return this->ChannelGroup::GetChannel<const WaveChannel>(iChannel); }

   auto Channels() {
      return this->ChannelGroup::Channels<WaveChannel>(); }
   auto Channels() const {
      return this->ChannelGroup::Channels<const WaveChannel>(); }

   AudioGraph::ChannelType GetChannelType() const override;

   //! Overwrite data excluding the sample sequence but including display
   //! settings
   /*!
    @pre `IsLeader()`
    @pre `orig.IsLeader()`
    @pre `NChannels() == orig.NChannels()`
    */
   void Reinit(const WaveTrack &orig);
 private:
   using ConstIterPair = std::pair<
      WaveClipHolders::const_iterator, WaveClipHolders::const_iterator>;
   /*!
    @param clip is searched for in the leader channel
    @pre `IsLeader()`
    @return first is iterator to clip if found, and if stereo, second is the
       corresponding clip of the other channel
    */
   ConstIterPair FindWideClip(const WaveClip &clip, int *pDistance = nullptr)
      const;

   using IterPair = std::pair<
      WaveClipHolders::iterator, WaveClipHolders::iterator>;
   /*!
    @copydoc FindWideClip(const WaveClip &, int *)
    */
   IterPair FindWideClip(const WaveClip &clip, int *pDistance = nullptr);

   void RemoveWideClip(IterPair pair);

   void Init(const WaveTrack &orig);

   TrackListHolder Clone() const override;

   friend class WaveTrackFactory;

   wxString MakeClipCopyName(const wxString& originalName) const;
   wxString MakeNewClipName() const;
 public:

   using Holder = std::shared_ptr<WaveTrack>;

   virtual ~WaveTrack();

   void MoveTo(double o) override;

   bool LinkConsistencyFix(bool doFix) override;

   //! Implement WideSampleSequence
   double GetStartTime() const override;
   //! Implement WideSampleSequence
   double GetEndTime() const override;

   //
   // Identifying the type of track
   //

   //
   // WaveTrack parameters
   //

   double GetRate() const override;
   ///!brief Sets the new rate for the track without resampling it
   /*!
    @pre `IsLeader()`
    @pre newRate > 0
    */
   void SetRate(double newRate);

   // Multiplicative factor.  Only converted to dB for display.
   float GetGain() const;
   void SetGain(float newGain);

   // -1.0 (left) -> 1.0 (right)
   float GetPan() const;
   void SetPan(float newPan);

   //! Takes gain and pan into account
   float GetChannelGain(int channel) const override;

   int GetWaveColorIndex() const;
   /*!
    @pre `IsLeader()`
    */
   void SetWaveColorIndex(int colorIndex);

   sampleCount GetVisibleSampleCount() const;

   sampleFormat GetSampleFormat() const override;

   /*!
    @pre `IsLeader()`
    */
   void ConvertToSampleFormat(sampleFormat format,
      const std::function<void(size_t)> & progressReport = {});

   //
   // High-level editing
   //

   TrackListHolder Cut(double t0, double t1) override;

   //! Make another track copying format, rate, color, etc. but containing no
   //! clips; and always with a unique channel
   /*!
    It is important to pass the correct factory (that for the project
    which will own the copy) in the unusual case that a track is copied from
    another project or the clipboard.  For copies within one project, the
    default will do.

    @param keepLink if false, make the new track mono.  But always preserve
    any other track group data.
    */
   Holder EmptyCopy(const SampleBlockFactoryPtr &pFactory = {},
      bool keepLink = true) const;

   //! Make another channel group copying format, rate, color, etc. but
   //! containing no clips; with as many channels as in `this`
   /*!
    It is important to pass the correct factory (that for the project
    which will own the copy) in the unusual case that a track is copied from
    another project or the clipboard.  For copies within one project, the
    default will do.

    @param keepLink if false, make the new track mono.  But always preserve
    any other track group data.

    @pre `IsLeader()`
    */
   TrackListHolder WideEmptyCopy(const SampleBlockFactoryPtr &pFactory = {},
      bool keepLink = true) const;

   //! @pre !GetOwner()
   TrackListHolder MonoToStereo();

   // If forClipboard is true,
   // and there is no clip at the end time of the selection, then the result
   // will contain a "placeholder" clip whose only purpose is to make
   // GetEndTime() correct.  This clip is not re-copied when pasting.
   TrackListHolder Copy(double t0, double t1, bool forClipboard = true)
      const override;

   void Clear(double t0, double t1) override;
   void Paste(double t0, const Track &src) override;
   using Track::Paste; // Get the non-virtual overload too

   /*!
    May assume precondition: t0 <= t1
    If the source has one channel and this has more, then replicate source
    @pre `IsLeader()`
    @pre `src.IsLeader()`
    @pre `src.NChannels() == 1 || src.NChannels() == NChannels()`
    */
   void ClearAndPaste(
      double t0, double t1, const WaveTrack& src, bool preserve = true,
      bool merge = true, const TimeWarper* effectWarper = nullptr,
      bool clearByTrimming = false) /* not override */;
   /*!
    Overload that takes a TrackList and passes its first wave track
    @pre `**src.Any<const WaveTrack>().begin()` satisfies preconditions
    of the other overload for `src`
    */
   void ClearAndPaste(double t0, double t1,
      const TrackList &src,
      bool preserve = true,
      bool merge = true,
      const TimeWarper *effectWarper = nullptr)
   {
      ClearAndPaste(t0, t1, **src.Any<const WaveTrack>().begin(),
         preserve, merge, effectWarper);
   }

   void Silence(double t0, double t1, ProgressReporter reportProgress) override;
   void InsertSilence(double t, double len) override;

   /*!
    @pre `IsLeader()`
    */
   void Split(double t0, double t1);

   /*!
    @pre `IsLeader()`
    */
   std::pair<IntervalHolder, IntervalHolder> SplitAt(double t);

   /*!
    May assume precondition: t0 <= t1
    @pre `IsLeader()`
    */
   void ClearAndAddCutLine(double t0, double t1) /* not override */;

   /*!
    @pre `IsLeader()`
    @post result: `result->NChannels() == NChannels()`
    */
   TrackListHolder SplitCut(double t0, double t1) /* not override */;

   // May assume precondition: t0 <= t1
   /*!
    @pre `IsLeader()`
    */
   void SplitDelete(double t0, double t1) /* not override */;
   /*!
    @pre `IsLeader()`
    */
   void Join(
      double t0, double t1,
      const ProgressReporter& reportProgress) /* not override */;
   // May assume precondition: t0 <= t1
   /*!
    @pre `IsLeader()`
    */
   void Disjoin(double t0, double t1) /* not override */;

   // May assume precondition: t0 <= t1
   /*!
    @pre `IsLeader()`
    */
   void Trim(double t0, double t1) /* not override */;

   /*
    * @param interval Entire track is rendered if nullopt, else only samples
    * within [interval->first, interval->second), in which case clips are split
    * at these boundaries before rendering - if rendering is needed.
    *
    * @pre `!interval.has_value() || interval->first <= interval->second`
    */
   void ApplyStretchRatio(
      std::optional<TimeInterval> interval, ProgressReporter reportProgress);

   void SyncLockAdjust(double oldT1, double newT1) override;

   /** @brief Returns true if there are no WaveClips in the specified region
    *
    * @return true if no clips in the track overlap the specified time range,
    * false otherwise.
    */
   bool IsEmpty(double t0, double t1) const;

   /*!
    If there is an existing WaveClip in the WaveTrack,
    then the data are appended to that clip. If there are no WaveClips in the
    track, then a new one is created.
    @return true if at least one complete block was created
    */
   bool Append(size_t iChannel, constSamplePtr buffer, sampleFormat format,
      size_t len, unsigned int stride = 1,
      sampleFormat effectiveFormat = widestSampleFormat)
   override;

   void Flush() override;

   bool IsLeader() const override;

   //! @name PlayableSequence implementation
   //! @{
   const ChannelGroup *FindChannelGroup() const override;
   bool GetMute() const override;
   bool GetSolo() const override;
   //! @}

   ///
   /// MM: Now that each wave track can contain multiple clips, we don't
   /// have a continuous space of samples anymore, but we simulate it,
   /// because there are a lot of places (e.g. effects) using this interface.
   /// This interface makes much sense for modifying samples, but note that
   /// it is not time-accurate, because the "offset" is a double value and
   /// therefore can lie inbetween samples. But as long as you use the
   /// same value for "start" in both calls to "Set" and "Get" it is
   /// guaranteed that the same samples are affected.
   ///

   //! This fails if any clip overlapping the range has non-unit stretch ratio!
   bool DoGet(
      size_t iChannel, size_t nBuffers, const samplePtr buffers[],
      sampleFormat format, sampleCount start, size_t len, bool backwards,
      fillFormat fill = FillFormat::fillZero, bool mayThrow = true,
      // Report how many samples were copied from within clips, rather than
      // filled according to fillFormat; but these were not necessarily one
      // contiguous range.
      sampleCount* pNumWithinClips = nullptr) const override;

   /*!
    * @brief Request samples within [t0, t1), not knowing in advance how
    * many this will be.
    *
    * @details The stretching of intersecting intervals influences the number of
    * samples fitting into [t0, t1), i.e., half as many for twice as large a
    * stretch ratio, due to a larger spacing of the raw samples.
    *
    * @pre `IsLeader()`
    * @post result: `result.size() == NChannels()`
    * @pre samples in [t0, t1) can be counted with `size_t`
    */
   ChannelGroupSampleView
   GetSampleView(double t0, double t1, bool mayThrow = true) const;

   sampleFormat WidestEffectiveFormat() const override;

   bool HasTrivialEnvelope() const override;

   void GetEnvelopeValues(
      double* buffer, size_t bufferLen, double t0,
      bool backwards) const override;

   const WaveClip* GetClipAtTime(double time) const;
   WaveClip* GetClipAtTime(double time);
   WaveClipConstHolders GetClipsIntersecting(double t0, double t1) const;

   //
   // Getting information about the track's internal block sizes
   // and alignment for efficiency
   //

   // These return a nonnegative number of samples meant to size a memory buffer
   size_t GetBestBlockSize(sampleCount t) const;
   size_t GetMaxBlockSize() const;
   size_t GetIdealBlockSize();

   //
   // XMLTagHandler callback methods for loading and saving
   //

   bool HandleXMLTag(const std::string_view& tag, const AttributesList& attrs) override;
   void HandleXMLEndTag(const std::string_view& tag) override;
   XMLTagHandler *HandleXMLChild(const std::string_view& tag) override;
   void WriteXML(XMLWriter &xmlFile) const override;

   // Returns true if an error occurred while reading from XML
   std::optional<TranslatableString> GetErrorOpening() const override;

   //
   // Lock and unlock the track: you must lock the track before
   // doing a copy and paste between projects.
   //

   //! Get access to the (visible) clips in the tracks, in unspecified order
   //! (not necessarily sequenced in time).
   /*!
    @post all pointers are non-null
    */
   WaveClipHolders &GetClips() { return mClips; }
   /*!
    @copydoc GetClips
    */
   const WaveClipConstHolders &GetClips() const
      { return reinterpret_cast< const WaveClipConstHolders& >( mClips ); }

   IntervalHolder GetLeftmostClip();
   IntervalConstHolder GetLeftmostClip() const;

   IntervalHolder GetRightmostClip();
   IntervalConstHolder GetRightmostClip() const;

   /**
    * @brief Get access to the (visible) clips in the tracks, in unspecified
    * order.
    * @pre `IsLeader()`
    */
   ClipConstHolders GetClipInterfaces() const;

   /// @pre IsLeader()
   //! Create new clip and add it to this track.
   /*!
    Returns a pointer to the newly created clip. Optionally initial offset and
    clip name may be provided, and a clip from which to copy all sample data.
    @param offset desired sequence (not play) start time
    */
   IntervalHolder
   CreateWideClip(double offset = .0, const wxString& name = wxEmptyString,
      const Interval *pToCopy = nullptr, bool copyCutlines = true);

   /// @pre IsLeader()
   //! Create new clip and add it to this track.
   /*!
    Returns a pointer to the newly created clip, using this track's block
    factory but copying all else from the given clip, except possibly the
    cutlines.
    */
   IntervalHolder CopyClip(const Interval &toCopy, bool copyCutlines);

   //! Create new clip and add it to this track.
   /*!
    Returns a pointer to the newly created clip. Optionally initial offset and
    clip name may be provided

    @post result: `result->GetWidth() == GetWidth()`
    */
   WaveClipHolder
   CreateClip(double offset = .0, const wxString& name = wxEmptyString);

   /** @brief Get access to the most recently added clip, or create a clip,
   *  if there is not already one.  THIS IS NOT NECESSARILY RIGHTMOST.
   *
   *  @return a pointer to the most recently added WaveClip
   */
   WaveClip* NewestOrNewClip();

   /** @brief Get access to the last (rightmost) clip, or create a clip,
   *  if there is not already one.
   *
   *  @return a pointer to a WaveClip at the end of the track
   */
   WaveClip* RightmostOrNewClip();

   //! Get the nth clip in this WaveTrack (will return nullptr if not found).
   /*!
    Use this only in special cases (like getting the linked clip), because
    it is much slower than GetClipIterator().
    */
   WaveClip *GetClipByIndex(int index);
   /*!
    @copydoc GetClipByIndex
    */
   const WaveClip* GetClipByIndex(int index) const;

   // Get number of clips in this WaveTrack
   int GetNumClips() const;
   int GetNumClips(double t0, double t1) const;

   //! Return all WaveClips sorted by clip play start time.
   WaveClipPointers SortedClipArray();
   //! Return all WaveClips sorted by clip play start time.
   WaveClipConstPointers SortedClipArray() const;

   //! Return all (wide) WaveClips sorted by clip play start time.
   IntervalHolders SortedIntervalArray();
   //! Return all (wide) WaveClips sorted by clip play start time.
   IntervalConstHolders SortedIntervalArray() const;

   //! Decide whether the clips could be offset (and inserted) together without overlapping other clips
   /*!
   @return true if possible to offset by `(allowedAmount ? *allowedAmount : amount)`
    */
   bool CanOffsetClips(
      const std::vector<WaveClip*> &clips, //!< not necessarily in this track
      double amount, //!< signed
      double *allowedAmount = nullptr /*!<
         [out] if null, test exact amount only; else, largest (in magnitude) possible offset with same sign */
   );

   // Before moving a clip into a track (or inserting a clip), use this
   // function to see if the times are valid (i.e. don't overlap with
   // existing clips).
   bool
   CanInsertClip(const WaveClip& clip, double& slideBy, double tolerance) const;

   // Remove the clip from the track and return a SMART pointer to it.
   // You assume responsibility for its memory!
   std::shared_ptr<WaveClip> RemoveAndReturnClip(WaveClip* clip);

   //! Append a clip to the track; to succeed, must have the same block factory
   //! as this track, and `this->GetWidth() == clip->GetWidth()`; return success
   /*!
    @pre `clip != nullptr`
    @pre `this->GetWidth() == clip->GetWidth()`
    */
   bool AddClip(const std::shared_ptr<WaveClip> &clip);

   // Merge two clips, that is append data from clip2 to clip1,
   // then remove clip2 from track.
   // clipidx1 and clipidx2 are indices into the clip list.
   bool MergeClips(int clipidx1, int clipidx2);

   // Resample track (i.e. all clips in the track)
   void Resample(int rate, BasicUI::ProgressDialog *progress = NULL);

   //! Random-access assignment of a range of samples
   /*!
    @param buffers a span of pointers of size `NChannels()`
    @pre each of buffers is non-null
    */
   [[nodiscard]] bool SetFloats(const float *const *buffers,
      sampleCount start, size_t len,
      sampleFormat effectiveFormat = widestSampleFormat /*!<
         Make the effective format of the data at least the minumum of this
         value and `format`.  (Maybe wider, if merging with preexistent data.)
         If the data are later narrowed from stored format, but not narrower
         than the effective, then no dithering will occur.
      */
   );

   const TypeInfo &GetTypeInfo() const override;
   static const TypeInfo &ClassTypeInfo();

   class WAVE_TRACK_API Interval final : public WideChannelGroupInterval {
   public:
      /*!
       @pre `pClip != nullptr`
       */
      Interval(const ChannelGroup &group,
         const std::shared_ptr<WaveClip> &pClip,
         const std::shared_ptr<WaveClip> &pClip1);

      Interval(
         const ChannelGroup& group, size_t width,
         const SampleBlockFactoryPtr& factory, int rate,
         sampleFormat storedSampleFormat);

      Interval(const Interval &) = delete;
      Interval& operator=(const Interval &) = delete;

      ~Interval() override;

      //! An invariant condition, for assertions
      bool EqualSequenceLengthInvariant() const;

      void Append(constSamplePtr buffer[], sampleFormat format, size_t len);
      void Flush();

      /// This name is consistent with WaveTrack::Clear. It performs a "Cut"
      /// operation (but without putting the cut audio to the clipboard)
      void Clear(double t0, double t1);

      void SetName(const wxString& name);
      const wxString& GetName() const;

      void SetColorIndex(int index);
      int GetColorIndex() const;

      void SetPlayStartTime(double time);
      double GetPlayStartTime() const;
      double GetPlayEndTime() const;

      //! Real start time of the clip, quantized to raw sample rate (track's rate)
      sampleCount GetPlayStartSample() const;

      //! Real end time of the clip, quantized to raw sample rate (track's rate)
      sampleCount GetPlayEndSample() const;

      /*!
       * @brief [ < t and t < ), such that if the track were split at `t`, it would
       * split this clip in two of lengths > 0.
       */
      bool SplitsPlayRegion(double t) const;
      /*!
       * @brief  t ∈ [...)
       */
      bool WithinPlayRegion(double t) const;
      /*!
       * @brief  t < [
       */
      bool BeforePlayRegion(double t) const;
      /*!
       * @brief  t <= [
       */
      bool AtOrBeforePlayRegion(double t) const;
      /*!
       * @brief  ) <= t
       */
      bool AfterPlayRegion(double t) const;
      /*!
       * @brief t0 and t1 both ∈ [...)
       * @pre t0 <= t1
       */
      bool EntirelyWithinPlayRegion(double t0, double t1) const;
      /*!
       * @brief t0 xor t1 ∈ [...)
       * @pre t0 <= t1
       */
      bool PartlyWithinPlayRegion(double t0, double t1) const;
      /*!
       * @brief [t0, t1) ∩ [...) != ∅
       * @pre t0 <= t1
       */
      bool IntersectsPlayRegion(double t0, double t1) const;
      /*!
       * @brief t0 <= [ and ) <= t1, such that removing [t0, t1) from the track
       * deletes this clip.
       * @pre t0 <= t1
       */
      bool CoversEntirePlayRegion(double t0, double t1) const;

      double GetStretchRatio() const;
      void SetRawAudioTempo(double tempo);

      sampleCount TimeToSamples(double time) const;
      double SamplesToTime(sampleCount s) const;
      double GetSequenceStartTime() const;
      double GetSequenceEndTime() const;
      double GetTrimLeft() const;
      double GetTrimRight() const;

      auto GetChannel(size_t iChannel) { return
         WideChannelGroupInterval::GetChannel<WaveChannelInterval>(iChannel); }
      auto GetChannel(size_t iChannel) const { return
         WideChannelGroupInterval::GetChannel<const WaveChannelInterval>(iChannel); }

      auto Channels() { return
         WideChannelGroupInterval::Channels<WaveChannelInterval>(); }

      auto Channels() const { return
         WideChannelGroupInterval::Channels<const WaveChannelInterval>(); }

      bool IsPlaceholder() const;

      void SetSequenceStartTime(double t);
      void TrimLeftTo(double t);
      void TrimRightTo(double t);
      void TrimQuarternotesFromRight(double numQuarternotes);
      void StretchLeftTo(double t);
      void StretchRightTo(double t);
      void StretchBy(double ratio);
      void SetTrimLeft(double t);
      void SetTrimRight(double t);

      //! Moves play start position by deltaTime
      void TrimLeft(double deltaTime);
      //! Moves play end position by deltaTime
      void TrimRight(double deltaTime);

      //! Same as `TrimRight`, but expressed as quarter notes
      void ClearLeft(double t);
      void ClearRight(double t);

      /*!
       May assume precondition: t0 <= t1
       */
      void ClearAndAddCutLine(double t0, double t1);
   
      //! Argument is non-const because it must share mutative access to the
      //! underlying clip data
      //! @pre `NChannels() == interval.NChannels()`
      void AddCutLine(Interval &interval);
   
      /*!
       * @post result: `result->GetStretchRatio() == 1`
       */
      std::shared_ptr<Interval> GetStretchRenderedCopy(
         const std::function<void(double)>& reportProgress,
         const ChannelGroup& group, const SampleBlockFactoryPtr& factory,
         sampleFormat format);

      //! Checks for stretch-ratio equality, accounting for rounding errors.
      //! @{
      bool HasEqualStretchRatio(const Interval& other) const;
      bool StretchRatioEquals(double value) const;
      //! @}

      /*! @excsafety{No-fail} */
      void ShiftBy(double delta) noexcept;

      sampleCount GetSequenceSamplesCount() const;
      size_t CountBlocks() const;

      void ConvertToSampleFormat(sampleFormat format,
         const std::function<void(size_t)> & progressReport = {});

      //! Silences the 'length' amount of samples starting from 'offset'
      //! (relative to the play start)
      void SetSilence(sampleCount offset, sampleCount length);

      void CloseLock() noexcept;

      SampleFormats GetSampleFormats() const;

      /// Remove cut line, without expanding the audio in it
      /*!
       @return whether any cutline existed at the position and was removed
       */
      bool RemoveCutLine(double cutLinePosition);

      //! Construct an array of temporary Interval objects that point to
      //! the cutlines
      /*!
       @param track is required to construct new Intervals because this
       Interval does not store a back-reference to its track
       */
      std::vector<IntervalHolder> GetCutLines(WaveTrack &track);

      // Resample clip. This also will set the rate, but without changing
      // the length of the clip
      void Resample(int rate, BasicUI::ProgressDialog *progress = nullptr);

      std::shared_ptr<const WaveClip> GetClip(size_t iChannel) const
      { return iChannel == 0 ? mpClip : mpClip1; }
      const std::shared_ptr<WaveClip> &GetClip(size_t iChannel)
      { return iChannel == 0 ? mpClip : mpClip1; }

      /** Insert silence at the end, and causes the envelope to ramp
          linearly to the given value */
      void AppendSilence(double len, double envelopeValue);

      bool Paste(double t0, const Interval &src);

      const Envelope& GetEnvelope() const;

      /** Find cut line at (approximately) this position. Returns true and fills
       * in cutLineStart and cutLineEnd (if specified) if a cut line at this
       * position could be found. Return false otherwise. */
      bool FindCutLine(double cutLinePosition,
         double* cutLineStart = nullptr,
         double *cutLineEnd = nullptr) const;

      void ExpandCutLine(double cutlinePosition);

      void SetRate(int rate);

   private:
      // TODO wide wave tracks -- remove friend
      friend WaveTrack;

      void SetEnvelope(const Envelope& envelope);

      // Helper function in time of migration to wide clips
      template<typename Callable> void ForEachClip(const Callable& op) {
         for (size_t channel = 0, channelCount = NChannels();
            channel < channelCount; ++channel)
            op(*GetClip(channel));
      }

      // Helper function in time of migration to wide clips
      template<typename Callable> void ForEachClip(const Callable& op) const {
         for (size_t channel = 0, channelCount = NChannels();
            channel < channelCount; ++channel)
            op(*GetClip(channel));
      }

      // Helper function in time of migration to wide clips
      /*!
       @pre `src.NChannels() == dst.NChannels()`
       */
      template<typename Callable>
      static void ForCorrespondingClips(Interval &dst, const Interval &src,
         const Callable& binop)
      {
         const auto channelCount = src.NChannels();
         assert(channelCount == dst.NChannels());
         for (size_t channel = 0; channel < channelCount; ++channel)
            binop(*dst.GetClip(channel), *src.GetClip(channel));
      }

      std::shared_ptr<ChannelInterval> DoGetChannel(size_t iChannel) override;
      const std::shared_ptr<WaveClip> mpClip;
      //! TODO wide wave tracks: eliminate this
      const std::shared_ptr<WaveClip> mpClip1;
   };

   ///@return Interval that starts after(before) the beginning of the passed interval
   IntervalConstHolder GetNextInterval(
      const Interval& interval, PlaybackDirection searchDirection) const;

   /*!
    * @copydoc GetNextInterval(const Interval&, PlaybackDirection) const
    */
   IntervalHolder
   GetNextInterval(const Interval& interval, PlaybackDirection searchDirection);

   IntervalHolder GetIntervalAtTime(double t);

   auto Intervals() { return ChannelGroup::Intervals<Interval>(); }
   auto Intervals() const { return ChannelGroup::Intervals<const Interval>(); }

   //! @pre `IsLeader()`
   void InsertInterval(const IntervalHolder& interval);

   //! @pre `IsLeader()`
   void RemoveInterval(const IntervalHolder& interval);

   Track::Holder PasteInto(AudacityProject &project, TrackList &list)
      const override;

   //! Returns nullptr if clip with such name was not found
   const WaveClip* FindClipByName(const wxString& name) const;

   size_t NIntervals() const override;

   /*!
    @pre `IsLeader()`
    */
   IntervalHolder GetWideClip(size_t iInterval);
   /*!
    @pre `IsLeader()`
    */
   IntervalConstHolder GetWideClip(size_t iInterval) const;

   //!< used only during deserialization
   void SetLegacyFormat(sampleFormat format);

   // TODO wide-wave-track: some other API
   void CopyClipEnvelopes();

private:
   //! Get the linear index of a given clip (== number of clips if not found)
   int GetClipIndex(const Interval &clip) const;

   void FlushOne();
   // May assume precondition: t0 <= t1
   void HandleClear(
      double t0, double t1, bool addCutLines, bool split,
      bool clearByTrimming = false);

   /*
    * @brief Copy/Paste operations must preserve beat durations, but time
    * boundaries are expressed in seconds. For pasting to work, source and
    * destination tracks must therefore have equal tempo.
    * @pre Preconditions of `ClearAndPaste`
    * @pre `GetProjectTempo().has_value() && GetProjectTempo() ==
    * src.GetProjectTempo()`
    */
   void ClearAndPasteAtSameTempo(
      double t0, double t1, const WaveTrack& src, bool preserve, bool merge,
      const TimeWarper* effectWarper, bool clearByTrimming);

   //! @pre All clips intersecting [t0, t1) have unit stretch ratio
   static void JoinOne(WaveTrack& track, double t0, double t1);
   static Holder CopyOne(const WaveTrack &track,
      double t0, double t1, bool forClipboard);
   static void WriteOneXML(const WaveTrack &track, XMLWriter &xmlFile,
      size_t iChannel, size_t nChannels);
   std::pair<WaveClipHolder, WaveClipHolder> SplitOneAt(double t);
   void ExpandOneCutLine(double cutLinePosition,
      double* cutlineStart, double* cutlineEnd);
   void ApplyStretchRatioOnIntervals(
      const IntervalHolders& intervals,
      const ProgressReporter& reportProgress);
   //! @pre `IsLeader()`
   //! @pre `oldOne->NChannels() == newOne->NChannels()`
   void
   ReplaceInterval(const IntervalHolder& oldOne, const IntervalHolder& newOne);

   std::shared_ptr<WideChannelGroupInterval> DoGetInterval(size_t iInterval)
      override;
   std::shared_ptr<::Channel> DoGetChannel(size_t iChannel) override;

   ChannelGroup &DoGetChannelGroup() const override;
   ChannelGroup &ReallyDoGetChannelGroup() const override;

   //
   // Protected variables
   //

   /*!
    * Do not call `mClips.push_back` directly. Use `InsertClip` instead.
    * @invariant all are non-null and match `this->GetWidth()`
    */
   WaveClipHolders mClips;

   mutable int  mLegacyRate{ 0 }; //!< used only during deserialization
   sampleFormat mLegacyFormat{ undefinedSample }; //!< used only during deserialization

private:
   //Updates rate parameter only in WaveTrackData
   void DoSetRate(double newRate);
   void DoOnProjectTempoChange(
      const std::optional<double>& oldTempo, double newTempo) override;
   /*!
    * @pre `IsLeader()`
    * @param[out] leader
    */
   //! @pre `IsLeader()`
   [[nodiscard]] TrackListHolder
   DuplicateWithOtherTempo(double newTempo, WaveTrack*& leader) const;

   bool GetOne(
      samplePtr buffer, sampleFormat format, sampleCount start, size_t len,
      bool backwards, fillFormat fill, bool mayThrow,
      sampleCount* pNumWithinClips) const;

   void DoSetPan(float value);
   void DoSetGain(float value);

   /*
    @pre `other.NChannels() == 1 || other.NChannels() == NChannels()`
    @pre `IsLeader()`
    */
   void PasteWaveTrack(double t0, const WaveTrack &other, bool merge);
   /*
    * @copybrief ClearAndPasteAtSameTempo
    * @pre Preconditions of `PasteWaveTrack`
    * @pre `GetProjectTempo().has_value() && GetProjectTempo() ==
    * other.GetProjectTempo()`
    */
   void
   PasteWaveTrackAtSameTempo(double t0, const WaveTrack& other, bool merge);

   //! Whether all clips of a leader track have a common rate
   /*!
    @pre `IsLeader()`
    */
   bool RateConsistencyCheck() const;
   //! Whether all tracks in group and all clips have a common sample format
   /*!
    @pre `IsLeader()`
    */
   bool FormatConsistencyCheck() const;

   //! Adds clip to the track. Clip should be not empty or to be a placeholder.
   //! Sets project tempo on clip upon push. Use this instead of
   //! `mClips.push_back`.
   //! @returns true on success
   bool InsertClip(WaveClipHolder clip);

   void ApplyStretchRatioOne(
      double t0, double t1, const ProgressReporter& reportProgress);

   //! Used only in assertions checking invariants
   bool ClipsAreUnique() const;

   SampleBlockFactoryPtr mpFactory;

   wxCriticalSection mFlushCriticalSection;
   wxCriticalSection mAppendCriticalSection;
   double mLegacyProjectFileOffset;
};

ENUMERATE_TRACK_TYPE(WaveTrack);

WaveTrack &WaveChannel::GetTrack() {
   auto &result = static_cast<WaveTrack&>(ReallyDoGetChannelGroup());
   return result;
}

const WaveTrack &WaveChannel::GetTrack() const {
   auto &result = static_cast<const WaveTrack&>(ReallyDoGetChannelGroup());
   return result;
}

size_t WaveChannel::GetBestBlockSize(sampleCount t) const {
   return GetTrack().GetBestBlockSize(t);
}

size_t WaveChannel::GetIdealBlockSize() {
   return GetTrack().GetIdealBlockSize();
}

size_t WaveChannel::GetMaxBlockSize() const {
   return GetTrack().GetMaxBlockSize();
}

class ProjectRate;

class WAVE_TRACK_API WaveTrackFactory final
   : public ClientData::Base
{
 public:
   static WaveTrackFactory &Get( AudacityProject &project );
   static const WaveTrackFactory &Get( const AudacityProject &project );
   static WaveTrackFactory &Reset( AudacityProject &project );
   static void Destroy( AudacityProject &project );

   WaveTrackFactory(
      const ProjectRate& rate,
      const SampleBlockFactoryPtr &pFactory)
       : mRate{ rate }
       , mpFactory(pFactory)
   {
   }
   WaveTrackFactory( const WaveTrackFactory & ) = delete;
   WaveTrackFactory &operator=( const WaveTrackFactory & ) = delete;

   const SampleBlockFactoryPtr &GetSampleBlockFactory() const
   { return mpFactory; }

   /**
    * \brief Creates an unnamed empty WaveTrack with default sample format and default rate
    * \return Orphaned WaveTrack
    */
   std::shared_ptr<WaveTrack> Create();

   /**
    * \brief Creates an unnamed empty WaveTrack with custom sample format and custom rate
    * \param format Desired sample format
    * \param rate Desired sample rate
    * \return Orphaned WaveTrack
    */
   std::shared_ptr<WaveTrack> Create(sampleFormat format, double rate);

   /**
    * \brief Creates new \p nChannels tracks with project's default rate and format.
    * If number of channels is exactly two then a single stereo track is created
    * instead.
    */
   TrackListHolder Create(size_t nChannels);

   /**
    * \brief Creates new \p nChannels tracks with specified \p format and
    * \p rate and places them into TrackList.
    * If number of channels is exactly two then a single stereo track is created
    * instead.
    */
   TrackListHolder Create(size_t nChannels, sampleFormat format, double rate);

   /**
    * \brief Creates new \p nChannels tracks by creating empty copies of \p proto.
    * If number of channels is exactly two then a single stereo track is created
    * instead.
    */
   TrackListHolder Create(size_t nChannels, const WaveTrack& proto);

 private:
   const ProjectRate &mRate;
   SampleBlockFactoryPtr mpFactory;
};

extern WAVE_TRACK_API BoolSetting
     EditClipsCanMove
;

extern WAVE_TRACK_API StringSetting AudioTrackNameSetting;

WAVE_TRACK_API bool GetEditClipsCanMove();

// Generate a registry for serialized data
#include "XMLMethodRegistry.h"
using WaveTrackIORegistry = XMLMethodRegistry<WaveTrack>;
DECLARE_XML_METHOD_REGISTRY( WAVE_TRACK_API, WaveTrackIORegistry );

#endif // __AUDACITY_WAVETRACK__
