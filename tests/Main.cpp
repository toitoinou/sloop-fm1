// SPDX-License-Identifier: GPL-3.0-only
// The unit tests (ctest, or build/SloopTests_artefacts/.../SloopTests): the app's rules on plain data, without the
// audio device or the screens. The app's --selftest checks the whole of the audio side end to end.
#include <juce_core/juce_core.h>

int main (int argc, char** argv)
{
    juce::ignoreUnused (argc, argv);
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();
    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i) failures += runner.getResult (i)->failures;
    std::printf ("%s\n", failures == 0 ? "all tests passed" : "TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
