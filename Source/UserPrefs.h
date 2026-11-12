#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// SPLMeter's own preferences file (window layout, theme, last folder, and a copy
// of the full processor state).
//
// It deliberately does NOT share a file with JUCE's standalone wrapper
// ("SPLMeter.settings"): the wrapper keeps that file open for the whole session
// and rewrites it from its in-memory copy on quit, which silently reverted
// anything we had written there in the meantime.
namespace UserPrefs
{
    inline juce::PropertiesFile::Options options()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "SPLMeter";
        o.filenameSuffix      = "prefs";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }

    // Pre-3.5.2 location, read once to migrate existing settings.
    inline juce::PropertiesFile::Options legacyOptions()
    {
        auto o = options();
        o.filenameSuffix = "settings";
        return o;
    }

    inline juce::File lastUsedFolder()
    {
        juce::PropertiesFile prefs (options());
        const auto path = prefs.getValue ("lastUsedFolder");
        const juce::File f (path);
        return (path.isNotEmpty() && f.isDirectory())
                   ? f : juce::File::getSpecialLocation (juce::File::userDesktopDirectory);
    }

    inline void saveLastUsedFolder (const juce::File& fileOrFolder)
    {
        juce::PropertiesFile prefs (options());
        prefs.setValue ("lastUsedFolder",
            (fileOrFolder.isDirectory() ? fileOrFolder
                                        : fileOrFolder.getParentDirectory()).getFullPathName());
    }

    //==========================================================================
    // Bind a control to a property of the processor's UIState tree: the saved
    // value is applied now (running the control's existing callback) and every
    // later change is recorded. Call after the control's callback is set up.
    inline void bind (SPLMeterAudioProcessor& p, juce::ComboBox& c, const juce::Identifier& key)
    {
        const int saved = p.getUiState().getProperty (key, 0);
        if (c.indexOfItemId (saved) >= 0 && saved != c.getSelectedId())
            c.setSelectedId (saved, juce::sendNotificationSync);

        c.onChange = [&p, &c, key, prev = std::move (c.onChange)]
        {
            p.getUiState().setProperty (key, c.getSelectedId(), nullptr);
            if (prev) prev();
        };
    }

    inline void bind (SPLMeterAudioProcessor& p, juce::Slider& s, const juce::Identifier& key)
    {
        auto ui = p.getUiState();
        if (ui.hasProperty (key))
            s.setValue ((double) ui.getProperty (key), juce::sendNotificationSync);

        s.onValueChange = [&p, &s, key, prev = std::move (s.onValueChange)]
        {
            p.getUiState().setProperty (key, s.getValue(), nullptr);
            if (prev) prev();
        };
    }

    inline void bind (SPLMeterAudioProcessor& p, juce::Button& b, const juce::Identifier& key)
    {
        auto ui = p.getUiState();
        if (ui.hasProperty (key) && (bool) ui.getProperty (key) != b.getToggleState())
        {
            b.setToggleState ((bool) ui.getProperty (key), juce::dontSendNotification);
            if (b.onClick) b.onClick();
        }

        b.onClick = [&p, &b, key, prev = std::move (b.onClick)]
        {
            p.getUiState().setProperty (key, b.getToggleState(), nullptr);
            if (prev) prev();
        };
    }
}
