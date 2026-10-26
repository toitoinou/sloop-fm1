// SPDX-License-Identifier: GPL-3.0-only
#if defined(__ANDROID__)
 #define JUCE_CORE_INCLUDE_JNI_HELPERS 1                 // (the Wi-Fi multicast lock below: JNI)
 #define DONT_SET_USING_JUCE_NAMESPACE 1
 #include <JuceHeader.h>
#endif
#include "audio/LinkSync.h"
#include <ableton/Link.hpp>
#include <ableton/link/HostTimeFilter.hpp>

namespace sloop
{
using us = std::chrono::microseconds;

struct LinkSync::Impl
{
    explicit Impl (double bpm) : link (bpm) { link.enableStartStopSync (true); }
    ableton::Link link;
    ableton::link::HostTimeFilter<ableton::link::platform::Clock> filter;
};

#if defined(__ANDROID__)
namespace
{
// Link finds the other apps by multicast: Android's Wi-Fi drops it unless an app holds a MulticastLock
void multicastLock (bool on)
{
    static jobject lock = nullptr;                       // (a global reference, kept)
    auto* env = juce::getEnv();
    if (env == nullptr) return;
    auto cleared = [env] { if (env->ExceptionCheck()) { env->ExceptionClear(); return false; } return true; };
    const juce::LocalRef<jclass> lockClass (env->FindClass ("android/net/wifi/WifiManager$MulticastLock"));
    if (! cleared() || lockClass == nullptr) return;
    if (lock == nullptr)
    {
        if (! on) return;
        const auto context = juce::getAppContext();
        const juce::LocalRef<jclass> contextClass (env->FindClass ("android/content/Context"));
        const juce::LocalRef<jstring> wifiName (env->NewStringUTF ("wifi"));
        const juce::LocalRef<jobject> wifi (env->CallObjectMethod (context.get(),
            env->GetMethodID (contextClass.get(), "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;"), wifiName.get()));
        if (! cleared() || wifi == nullptr) return;
        const juce::LocalRef<jclass> wifiClass (env->FindClass ("android/net/wifi/WifiManager"));
        const juce::LocalRef<jstring> tag (env->NewStringUTF ("SLOOP Touch: Ableton Link"));
        const juce::LocalRef<jobject> l (env->CallObjectMethod (wifi.get(), env->GetMethodID (wifiClass.get(),
            "createMulticastLock", "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;"), tag.get()));
        if (! cleared() || l == nullptr) return;
        lock = env->NewGlobalRef (l.get());
        env->CallVoidMethod (lock, env->GetMethodID (lockClass.get(), "setReferenceCounted", "(Z)V"), JNI_FALSE);
        cleared();
    }
    env->CallVoidMethod (lock, env->GetMethodID (lockClass.get(), on ? "acquire" : "release", "()V"));
    cleared();
}
} // namespace
#endif

LinkSync::LinkSync (double bpm) : impl (std::make_unique<Impl> (bpm)) {}
LinkSync::~LinkSync() { impl->link.enable (false); }

void LinkSync::enable (bool on)
{
#if defined(__ANDROID__)
    multicastLock (on);
#endif
    impl->link.enable (on);
}
bool LinkSync::isEnabled() const { return impl->link.isEnabled(); }
int LinkSync::peers() const { return (int) impl->link.numPeers(); }

int64_t LinkSync::now() const { return impl->link.clock().micros().count(); }
int64_t LinkSync::frameTime (double frame) { return impl->filter.sampleTimeToHostTime (frame).count(); }
void LinkSync::resetFilter() { impl->filter.reset(); }

LinkSync::State LinkSync::capture (int64_t start, int64_t end) const
{
    const auto s = impl->link.captureAudioSessionState();
    return { s.isPlaying(), s.tempo(), s.beatAtTime (us (start), kQuantum), s.beatAtTime (us (end), kQuantum) };
}
void LinkSync::setTempo (double bpm, int64_t at)
{
    auto s = impl->link.captureAudioSessionState();
    s.setTempo (bpm, us (at));
    impl->link.commitAudioSessionState (s);
}
void LinkSync::setPlaying (bool on, int64_t at)
{
    auto s = impl->link.captureAudioSessionState();
    if (on) s.setIsPlayingAndRequestBeatAtTime (true, us (at), 0.0, kQuantum);
    else s.setIsPlaying (false, us (at));
    impl->link.commitAudioSessionState (s);
}
} // namespace sloop
