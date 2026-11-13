#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UserPrefs.h"
#include "InputsComponent.h"
#include "Rew/RewSequence.h"
#include <atomic>
#include <mutex>

//==============================================================================
// "REW Measurement Sequencer" window (REW sequencer spec §4): connection status,
// step table, step editor, sequence options, run controls and run log.
namespace RewUi
{
using InputsUi::Palette;
using InputsUi::NumberField;
using InputsUi::U;

inline juce::Font uiFont (float h = 13.0f) { return juce::Font (juce::FontOptions().withHeight (h)); }

//==============================================================================
// Asks REW for its version, audio state and choice lists every 5 s while the
// window is open (spec §4). HTTP runs on this thread only.
class StatusPoller : private juce::Thread
{
public:
    struct State
    {
        bool checked = false, reachable = false, audioReady = false, blocking = false;
        rew::Version version;
        rew::Choices choices = rew::offlineChoices();
        int serial = 0;               // increments with every check
    };

    StatusPoller() : juce::Thread ("REW status") {}
    ~StatusPoller() override { stop(); }

    void start (int port)
    {
        port_ = port;
        if (! isThreadRunning())
            startThread();
        notify();
    }
    void stop()                 { signalThreadShouldExit(); notify(); stopThread (4000); }
    void setPort (int port)     { port_ = port; notify(); }
    void refreshNow()           { notify(); }

    State state() const
    {
        std::lock_guard<std::mutex> l (m_);
        return state_;
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            query();
            wait (5000);
        }
    }

    void query()
    {
        rew::HttpTransport t (port_, 1500);
        State s;
        s.checked = true;
        s.version = rew::parseVersion (t.get ("/version"));
        s.reachable = s.version.valid();
        if (s.reachable)
        {
            s.audioReady = (bool) t.get ("/audio/status").json().getProperty ("ready", false);
            const auto b = t.get ("/application/blocking").json();
            s.blocking = b.isBool() && (bool) b;
            s.choices = rew::queryChoices (t);
        }
        std::lock_guard<std::mutex> l (m_);
        s.serial = state_.serial + 1;
        state_ = s;
    }

    std::atomic<int> port_ { rew::HttpTransport::kDefaultPort };
    mutable std::mutex m_;
    State state_;
};

//==============================================================================
// Cell texts (spec §4 table); "—" = the step keeps REW's value
inline juce::String dash() { return U ("—"); }

inline juce::String hzText (int hz)
{
    return hz >= 1000 ? juce::String (hz / 1000.0, hz % 1000 == 0 ? 0 : 1) + "k" : juce::String (hz);
}

inline juce::String levelText (const rew::Level& l)
{
    return juce::String (l.value, std::abs (l.value - std::round (l.value)) < 1.0e-9 ? 0 : 1)
               .replace ("-", U ("−")) + " " + l.unit;
}

inline juce::String protectionText (const rew::Protection& p)
{
    juce::StringArray parts;
    if (p.clippingAbort) parts.add ("clip");
    if (p.splLimitAbort) parts.add (juce::String (p.dBSPLLimit, 0) + " dB");
    return parts.isEmpty() ? juce::String ("off") : parts.joinIntoString (", ");
}

//==============================================================================
class StepTable : public juce::Component,
                  private juce::TableListBoxModel
{
public:
    enum Col { cNum = 1, cOn, cName, cOut, cIn, cLevel, cSweep, cLen, cRep, cRef, cProt, cGroup, cWait };

    std::function<void (int)> onSelect;          // row or −1
    std::function<void (int)> onToggleEnabled;   // row

    StepTable()
    {
        table_.setModel (this);
        table_.setRowHeight (24);
        table_.setHeaderHeight (24);
        table_.setMultipleSelectionEnabled (false);
        auto& h = table_.getHeader();
        const int flags = juce::TableHeaderComponent::visible;
        h.addColumn ("#",      cNum,   32,  32,  48, flags);
        h.addColumn (U ("✓"),  cOn,    26,  26,  36, flags);
        h.addColumn ("Name",   cName,  104, 60,  400, flags | juce::TableHeaderComponent::resizable);
        h.addColumn ("Out",    cOut,   84,  32,  200, flags | juce::TableHeaderComponent::resizable);
        h.addColumn ("In",     cIn,    44,  32,  140, flags | juce::TableHeaderComponent::resizable);
        h.addColumn ("Level",  cLevel, 74,  50,  140, flags | juce::TableHeaderComponent::resizable);
        h.addColumn ("Sweep",  cSweep, 70,  50,  140, flags | juce::TableHeaderComponent::resizable);
        h.addColumn ("Len",    cLen,   42,  36,  80,  flags | juce::TableHeaderComponent::resizable);
        h.addColumn ("Rep",    cRep,   34,  30,  60,  flags);
        h.addColumn ("Ref",    cRef,   66,  40,  120, flags | juce::TableHeaderComponent::resizable);
        h.addColumn ("Prot",   cProt,  56,  40,  140, flags | juce::TableHeaderComponent::resizable);
        h.addColumn ("Group",  cGroup, 64,  40,  200, flags | juce::TableHeaderComponent::resizable);
        h.addColumn ("Wait",   cWait,  38,  34,  60,  flags);
        h.setStretchToFitActive (true);   // no horizontal scrolling
        addAndMakeVisible (table_);
    }

    void setSequence (const rew::Sequence* q, std::vector<bool> errorRows)
    {
        seq_ = q;
        errorRows_ = std::move (errorRows);
        table_.updateContent();
        table_.repaint();
    }

    // Row of the step being run (−1 = none)
    void setActiveRow (int row)
    {
        if (row == activeRow_) return;
        activeRow_ = row;
        table_.repaint();
    }

    void selectRow (int row)
    {
        if (row >= 0) table_.selectRow (row);
        else          table_.deselectAllRows();
    }
    int selectedRow() const { return table_.getSelectedRow(); }

    void applyPalette (const Palette& p)
    {
        pal_ = p;
        table_.setColour (juce::ListBox::backgroundColourId, p.card);
        table_.setColour (juce::ListBox::outlineColourId, p.border);
        table_.setOutlineThickness (1);
        auto& h = table_.getHeader();
        h.setColour (juce::TableHeaderComponent::backgroundColourId, p.field);
        h.setColour (juce::TableHeaderComponent::textColourId, p.subtext);
        h.setColour (juce::TableHeaderComponent::outlineColourId, p.border);
        repaint();
    }

    void resized() override { table_.setBounds (getLocalBounds()); }

private:
    int getNumRows() override { return seq_ != nullptr ? (int) seq_->steps.size() : 0; }

    void paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected) override
    {
        if (row == activeRow_)
            g.fillAll (pal_.ok.withAlpha (0.30f));
        else if (selected)
            g.fillAll (pal_.accent.withAlpha (0.28f));
        else if (row < (int) errorRows_.size() && errorRows_[(size_t) row])
            g.fillAll (pal_.bad.withAlpha (0.16f));
        else if (row % 2 == 1)
            g.fillAll (pal_.field.withAlpha (0.35f));
        g.setColour (pal_.border.withAlpha (0.5f));
        g.fillRect (0, h - 1, w, 1);
    }

    void paintCell (juce::Graphics& g, int row, int col, int w, int h, bool) override
    {
        if (seq_ == nullptr || row >= (int) seq_->steps.size())
            return;
        const auto& s = seq_->steps[(size_t) row];
        g.setColour (s.enabled ? pal_.text : pal_.subtext.withAlpha (0.7f));
        g.setFont (uiFont (13.0f));

        juce::String t;
        auto opt = [] (const auto& o, auto f) { return o ? f (*o) : dash(); };
        switch (col)
        {
            case cNum:   t = juce::String (row + 1); break;
            case cOn:    t = s.enabled ? U ("✓") : juce::String(); break;
            case cName:  t = rew::stepTitle (*seq_, row);
                         if (s.name.isEmpty()) g.setColour (pal_.subtext);
                         break;
            case cOut:   t = opt (s.output, [] (auto& v) { return v; });
                         if (s.secondOutput && s.secondOutput->enabled)
                             t << " + " << (s.secondOutput->invert ? U ("−") : juce::String()) << s.secondOutput->output;
                         else if (s.secondOutput && s.secondOutput->invert)
                             t << U (" (inv)");
                         break;
            case cIn:    t = s.input ? *s.input + (s.lastInput && *s.lastInput != *s.input ? U ("–") + *s.lastInput : juce::String())
                                     : dash();
                         break;
            case cLevel: t = opt (s.level, [] (auto& v) { return levelText (v); }); break;
            case cSweep: t = opt (s.sweep, [] (auto& v) { return hzText (v.startHz) + U ("–") + hzText (v.endHz); }); break;
            case cLen:   t = opt (s.sweep, [] (auto& v) { return v.length; }); break;
            case cRep:   t = opt (s.repetitions, [] (auto& v) { return juce::String (v); }); break;
            case cRef:   t = opt (s.timingRef, [] (auto& v) { return v; }); break;
            case cProt:  t = opt (s.protection, [] (auto& v) { return protectionText (v); }); break;
            case cGroup: t = s.group; break;
            case cWait:  t = s.waitForUser ? U ("✓") : juce::String(); break;
            default: break;
        }
        const auto just = (col == cName || col == cGroup) ? juce::Justification::centredLeft
                                                          : juce::Justification::centred;
        g.drawText (t, 4, 0, w - 8, h, just, true);
    }

    void cellClicked (int row, int col, const juce::MouseEvent&) override
    {
        if (col == cOn && onToggleEnabled)
            onToggleEnabled (row);
    }

    void selectedRowsChanged (int row) override { if (onSelect) onSelect (row); }

    juce::TableListBox table_ { "REW steps" };
    const rew::Sequence* seq_ = nullptr;
    std::vector<bool> errorRows_;
    int activeRow_ = -1;
    Palette pal_ = Palette::get (false);
};

//==============================================================================
// Editor for the selected step. Every REW setting has a "set" box: unticked,
// the step leaves REW's value as it is (spec §3).
class StepEditor : public juce::Component
{
public:
    std::function<void()> onChange;

    StepEditor()
    {
        for (auto* t : { &name_, &notes_, &group_, &prompt_ })
        {
            t->setFont (uiFont());
            t->onTextChange = [this] { commit(); };
            addAndMakeVisible (t);
        }
        name_.setTextToShowWhenEmpty ("(sequence name + number)", juce::Colours::grey);
        group_.setTextToShowWhenEmpty ("none", juce::Colours::grey);
        prompt_.setTextToShowWhenEmpty ("default prompt", juce::Colours::grey);
        notes_.setTextToShowWhenEmpty ("REW measurement notes", juce::Colours::grey);

        for (auto* b : { &enabled_, &setOut_, &setIn_, &setLevel_, &setSweep_, &setReps_, &setRef_, &setProt_,
                         &clip_, &splLimit_, &wait_, &setOut2_, &out2On_, &invert_ })
        {
            b->onClick = [this] { commit(); };
            addAndMakeVisible (b);
        }
        out2On_.setTooltip ("Play the sweep on a second output too (REW: ASIO driver only; with Java choose the output L+R)");
        invert_.setTooltip ("Invert the second output (REW: invert second output, also for L+R)");
        for (auto* c : { &out_, &inFirst_, &inLast_, &unit_, &length_, &ref_, &out2_ })
        {
            c->onChange = [this] { commit(); };
            addAndMakeVisible (c);
        }
        for (auto* c : { &out_, &inFirst_, &inLast_ })
            c->setEditableText (true);
        inLast_.setTextWhenNothingSelected (dash());
        for (auto* f : { &level_, &start_, &end_, &reps_, &limit_, &delay_ })
        {
            f->onValueChange = [this] (double) { commit(); };
            addAndMakeVisible (f);
        }
        level_.setValue (-12.0);
        start_.setValue (20.0);
        end_.setValue (20000.0);
        reps_.setValue (1.0);
        limit_.setValue (100.0);
        delay_.setValue (0.0);

        for (auto* l : { &lName_, &lNotes_, &lOut_, &lIn_, &lLevel_, &lSweep_, &lReps_, &lRef_, &lProt_,
                         &lGroup_, &lWait_, &lDelay_, &lTo_, &lHz_, &lOut2_ })
        {
            l->setFont (uiFont());
            addAndMakeVisible (l);
        }
        lTo_.setJustificationType (juce::Justification::centred);
        title_.setFont (uiFont (14.0f).boldened());
        addAndMakeVisible (title_);
        empty_.setText ("Select a step, or add one with + Add.", juce::dontSendNotification);
        empty_.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (empty_);
        setStep (nullptr, {}, {});
    }

    // s points into the window's sequence; the window calls this after every change
    // to the step list, so the pointer never outlives its vector
    void setStep (rew::Step* s, const juce::String& title, const rew::Choices& choices)
    {
        step_ = s;
        loading_ = true;
        fillChoices (choices);
        const bool has = s != nullptr;
        for (int i = 0; i < getNumChildComponents(); ++i)
            getChildComponent (i)->setVisible (has);
        empty_.setVisible (! has);
        if (has)
        {
            title_.setText ("Step: " + title, juce::dontSendNotification);
            name_.setText (s->name, false);
            notes_.setText (s->notes, false);
            group_.setText (s->group, false);
            prompt_.setText (s->prompt, false);
            enabled_.setToggleState (s->enabled, juce::dontSendNotification);
            wait_.setToggleState (s->waitForUser, juce::dontSendNotification);
            delay_.setValue (s->preDelaySec);

            setOut_.setToggleState (s->output.has_value(), juce::dontSendNotification);
            out_.setText (s->output.value_or (choices.outputs.isEmpty() ? "L" : choices.outputs[0]), juce::dontSendNotification);

            const auto so = s->secondOutput.value_or (rew::SecondOutput {});
            setOut2_.setToggleState (s->secondOutput.has_value(), juce::dontSendNotification);
            out2On_.setToggleState (so.enabled, juce::dontSendNotification);
            out2_.setText (so.output.isNotEmpty() ? so.output
                                                  : (choices.outputs.size() > 1 ? choices.outputs[1] : juce::String()),
                           juce::dontSendNotification);
            invert_.setToggleState (so.invert, juce::dontSendNotification);

            setIn_.setToggleState (s->input.has_value(), juce::dontSendNotification);
            inFirst_.setText (s->input.value_or (choices.driver == rew::Driver::Asio && ! choices.inputs.isEmpty()
                                                     ? choices.inputs[0] : juce::String ("1")),
                              juce::dontSendNotification);
            inLast_.setText (s->lastInput.value_or (juce::String()), juce::dontSendNotification);

            const auto lv = s->level.value_or (rew::Level {});
            setLevel_.setToggleState (s->level.has_value(), juce::dontSendNotification);
            level_.setValue (lv.value);
            unit_.setText (lv.unit, juce::dontSendNotification);

            const auto sw = s->sweep.value_or (rew::Sweep {});
            setSweep_.setToggleState (s->sweep.has_value(), juce::dontSendNotification);
            start_.setValue (sw.startHz);
            end_.setValue (sw.endHz);
            length_.setText (sw.length, juce::dontSendNotification);

            setReps_.setToggleState (s->repetitions.has_value(), juce::dontSendNotification);
            reps_.setValue (s->repetitions.value_or (1));

            setRef_.setToggleState (s->timingRef.has_value(), juce::dontSendNotification);
            ref_.setText (s->timingRef.value_or (choices.timingRefs.contains ("Acoustic") ? "Acoustic"
                                                                                          : choices.timingRefs[0]),
                          juce::dontSendNotification);

            const auto pr = s->protection.value_or (rew::Protection {});
            setProt_.setToggleState (s->protection.has_value(), juce::dontSendNotification);
            clip_.setToggleState (pr.clippingAbort, juce::dontSendNotification);
            splLimit_.setToggleState (pr.splLimitAbort, juce::dontSendNotification);
            limit_.setValue (pr.dBSPLLimit);
            updateEnablement();
        }
        loading_ = false;
    }

    void setTitle (const juce::String& title) { title_.setText ("Step: " + title, juce::dontSendNotification); }

    void applyPalette (const Palette& p)
    {
        pal_ = p;
        for (auto* t : { &name_, &notes_, &group_, &prompt_ })
        {
            t->setColour (juce::TextEditor::backgroundColourId, p.field);
            t->setColour (juce::TextEditor::textColourId, p.text);
            t->setColour (juce::TextEditor::outlineColourId, p.border);
            t->setColour (juce::TextEditor::focusedOutlineColourId, p.accent);
            t->applyColourToAllText (p.text);
        }
        for (auto* c : { &out_, &inFirst_, &inLast_, &unit_, &length_, &ref_, &out2_ })
            p.style (*c);
        for (auto* f : { &level_, &start_, &end_, &reps_, &limit_, &delay_ })
            f->style (p);
        for (auto* b : { &enabled_, &setOut_, &setIn_, &setLevel_, &setSweep_, &setReps_, &setRef_, &setProt_,
                         &clip_, &splLimit_, &wait_, &setOut2_, &out2On_, &invert_ })
        {
            b->setColour (juce::ToggleButton::textColourId, p.text);
            b->setColour (juce::ToggleButton::tickColourId, p.accent);
            b->setColour (juce::ToggleButton::tickDisabledColourId, p.subtext);
        }
        for (auto* l : { &lName_, &lNotes_, &lOut_, &lIn_, &lLevel_, &lSweep_, &lReps_, &lRef_, &lProt_,
                         &lGroup_, &lWait_, &lDelay_, &lTo_, &lHz_, &lOut2_ })
            l->setColour (juce::Label::textColourId, p.subtext);
        title_.setColour (juce::Label::textColourId, p.text);
        empty_.setColour (juce::Label::textColourId, p.subtext);
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10);
        empty_.setBounds (r);
        auto top = r.removeFromTop (24);
        enabled_.setBounds (top.removeFromRight (90));
        title_.setBounds (top);
        r.removeFromTop (6);

        const int rowH = 28, labelW = 92, setW = 26;
        auto row = [&] (juce::Label& l) { auto x = r.removeFromTop (rowH); l.setBounds (x.removeFromLeft (labelW)); return x.reduced (0, 2); };
        auto withSet = [&] (juce::Rectangle<int>& x, juce::ToggleButton& b) { b.setBounds (x.removeFromLeft (setW)); };

        name_.setBounds (row (lName_));
        notes_.setBounds (row (lNotes_));
        r.removeFromTop (6);
        { auto x = row (lOut_);   withSet (x, setOut_);   out_.setBounds (x.removeFromLeft (150)); }
        { auto x = row (lOut2_);  withSet (x, setOut2_);  out2On_.setBounds (x.removeFromLeft (48));
                                  out2_.setBounds (x.removeFromLeft (150)); x.removeFromLeft (8);
                                  invert_.setBounds (x.removeFromLeft (80)); }
        { auto x = row (lIn_);    withSet (x, setIn_);    inFirst_.setBounds (x.removeFromLeft (90));
                                  lTo_.setBounds (x.removeFromLeft (24)); inLast_.setBounds (x.removeFromLeft (90)); }
        { auto x = row (lLevel_); withSet (x, setLevel_); level_.setBounds (x.removeFromLeft (80));
                                  x.removeFromLeft (6); unit_.setBounds (x.removeFromLeft (80)); }
        { auto x = row (lSweep_); withSet (x, setSweep_); start_.setBounds (x.removeFromLeft (64));
                                  x.removeFromLeft (2);   // en dash between
                                  end_.setBounds (x.removeFromLeft (70)); lHz_.setBounds (x.removeFromLeft (26));
                                  length_.setBounds (x.removeFromLeft (76)); }
        { auto x = row (lReps_);  withSet (x, setReps_);  reps_.setBounds (x.removeFromLeft (60)); }
        { auto x = row (lRef_);   withSet (x, setRef_);   ref_.setBounds (x.removeFromLeft (150)); }
        { auto x = row (lProt_);  withSet (x, setProt_);  clip_.setBounds (x.removeFromLeft (92));
                                  splLimit_.setBounds (x.removeFromLeft (88)); limit_.setBounds (x.removeFromLeft (70)); }
        r.removeFromTop (6);
        group_.setBounds (row (lGroup_).removeFromLeft (220));
        { auto x = row (lWait_);  wait_.setBounds (x.removeFromLeft (setW)); prompt_.setBounds (x); }
        delay_.setBounds (row (lDelay_).removeFromLeft (80));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (pal_.card);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (pal_.border);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

private:
    void fillChoices (const rew::Choices& c)
    {
        auto fill = [] (juce::ComboBox& box, const juce::StringArray& items)
        {
            const auto text = box.getText();
            box.clear (juce::dontSendNotification);
            int id = 1;
            for (const auto& s : items) box.addItem (s, id++);
            box.setText (text, juce::dontSendNotification);
        };
        fill (out_, c.outputs);
        fill (out2_, c.outputs);
        asio_ = c.driver == rew::Driver::Asio;
        juce::StringArray inputs = c.inputs;
        if (c.driver != rew::Driver::Asio)
        {
            inputs.clear();
            for (int i = 1; i <= 16; ++i) inputs.add (juce::String (i));
        }
        fill (inFirst_, inputs);
        fill (inLast_, inputs);
        fill (unit_, c.levelUnits);
        fill (length_, c.sweepLengths);
        fill (ref_, c.timingRefs);
    }

    void updateEnablement()
    {
        out_.setEnabled (setOut_.getToggleState());
        // Java: two outputs only via "L+R", so only "invert" can be set
        const bool java = ! asio_;
        out2On_.setEnabled (setOut2_.getToggleState() && (! java || out2On_.getToggleState()));
        out2_.setEnabled (setOut2_.getToggleState() && out2On_.getToggleState());
        invert_.setEnabled (setOut2_.getToggleState());
        inFirst_.setEnabled (setIn_.getToggleState());
        inLast_.setEnabled (setIn_.getToggleState());
        level_.setEnabled (setLevel_.getToggleState());
        unit_.setEnabled (setLevel_.getToggleState());
        for (juce::Component* c : { (juce::Component*) &start_, (juce::Component*) &end_, (juce::Component*) &length_ })
            c->setEnabled (setSweep_.getToggleState());
        reps_.setEnabled (setReps_.getToggleState());
        ref_.setEnabled (setRef_.getToggleState());
        for (juce::Component* c : { (juce::Component*) &clip_, (juce::Component*) &splLimit_, (juce::Component*) &limit_ })
            c->setEnabled (setProt_.getToggleState());
        limit_.setEnabled (setProt_.getToggleState() && splLimit_.getToggleState());
        prompt_.setEnabled (wait_.getToggleState());
        for (auto* c : { (juce::Component*) &out2On_, (juce::Component*) &out2_, (juce::Component*) &invert_,
                         (juce::Component*) &out_, (juce::Component*) &inFirst_, (juce::Component*) &inLast_,
                         (juce::Component*) &level_, (juce::Component*) &unit_, (juce::Component*) &start_,
                         (juce::Component*) &end_, (juce::Component*) &length_, (juce::Component*) &reps_,
                         (juce::Component*) &ref_, (juce::Component*) &clip_, (juce::Component*) &splLimit_,
                         (juce::Component*) &limit_, (juce::Component*) &prompt_ })
            c->setAlpha (c->isEnabled() ? 1.0f : 0.4f);
    }

    void commit()
    {
        if (loading_ || step_ == nullptr)
            return;
        auto& s = *step_;
        s.enabled = enabled_.getToggleState();
        s.name = name_.getText().trim();
        s.notes = notes_.getText();
        s.group = group_.getText().trim();
        s.waitForUser = wait_.getToggleState();
        s.prompt = prompt_.getText().trim();
        s.preDelaySec = delay_.getValue();

        auto text = [] (juce::ComboBox& c) { return c.getText().trim(); };
        s.output = setOut_.getToggleState() ? std::optional<juce::String> (text (out_)) : std::nullopt;
        s.secondOutput = setOut2_.getToggleState()
                             ? std::optional<rew::SecondOutput> (rew::SecondOutput { out2On_.getToggleState(), text (out2_),
                                                                                     invert_.getToggleState() })
                             : std::nullopt;
        if (setIn_.getToggleState())
        {
            s.input = text (inFirst_);
            const auto last = text (inLast_);
            s.lastInput = last.isNotEmpty() && last != dash() && last != *s.input
                              ? std::optional<juce::String> (last) : std::nullopt;
        }
        else
        {
            s.input.reset();
            s.lastInput.reset();
        }
        s.level = setLevel_.getToggleState() ? std::optional<rew::Level> (rew::Level { level_.getValue(), text (unit_) })
                                             : std::nullopt;
        s.sweep = setSweep_.getToggleState()
                      ? std::optional<rew::Sweep> (rew::Sweep { (int) start_.getValue(), (int) end_.getValue(), text (length_) })
                      : std::nullopt;
        s.repetitions = setReps_.getToggleState() ? std::optional<int> ((int) reps_.getValue()) : std::nullopt;
        s.timingRef = setRef_.getToggleState() ? std::optional<juce::String> (text (ref_)) : std::nullopt;
        s.protection = setProt_.getToggleState()
                           ? std::optional<rew::Protection> (rew::Protection { clip_.getToggleState(),
                                                                               splLimit_.getToggleState(),
                                                                               limit_.getValue() })
                           : std::nullopt;
        updateEnablement();
        if (onChange) onChange();
    }

    rew::Step* step_ = nullptr;
    bool loading_ = false, asio_ = false;
    Palette pal_ = Palette::get (false);

    juce::Label title_, empty_;
    juce::Label lName_ { {}, "Name" }, lNotes_ { {}, "Notes" }, lOut_ { {}, "Output" }, lIn_ { {}, "Input" },
                lLevel_ { {}, "Level" }, lSweep_ { {}, "Sweep" }, lReps_ { {}, "Repetitions" },
                lRef_ { {}, "Timing ref." }, lProt_ { {}, "Protection" }, lGroup_ { {}, "REW group" },
                lWait_ { {}, "Wait for me" }, lDelay_ { {}, "Delay before" },
                lTo_ { {}, U ("to") }, lHz_ { {}, "Hz" }, lOut2_ { {}, "2nd output" };
    juce::TextEditor name_, notes_, group_, prompt_;
    juce::ToggleButton enabled_ { "Enabled" }, setOut_, setIn_, setLevel_, setSweep_, setReps_, setRef_, setProt_,
                       clip_ { "Clipping" }, splLimit_ { "SPL limit" }, wait_,
                       setOut2_, out2On_ { "On" }, invert_ { "Invert" };
    juce::ComboBox out_, inFirst_, inLast_, unit_, length_, ref_, out2_;
    NumberField level_ { -120.0, 20.0, 1 }, start_ { 1.0, 96000.0, 0 }, end_ { 1.0, 96000.0, 0 },
                reps_ { 1.0, rew::limits::kMaxRepetitions, 0 }, limit_ { 40.0, 160.0, 0, "dB" },
                delay_ { 0.0, rew::limits::kMaxDelaySec, 1, "s" };
};

//==============================================================================
class RewSequencerComponent : public juce::Component,
                              private juce::Timer
{
public:
    RewSequencerComponent (SPLMeterAudioProcessor& p, bool light) : processor_ (p)
    {
        seq_ = processor_.getRewSequence();

        // Header: status + port
        statusText_.setFont (uiFont());
        addAndMakeVisible (statusText_);
        portLabel_.setFont (uiFont());
        addAndMakeVisible (portLabel_);
        port_.setValue (seq_.port);
        port_.onValueChange = [this] (double v) { seq_.port = (int) v; poller_.setPort (seq_.port); changed (false); };
        addAndMakeVisible (port_);
        refresh_.onClick = [this] { poller_.refreshNow(); };
        refresh_.setTooltip ("Ask REW again now (otherwise every 5 s)");
        addAndMakeVisible (refresh_);

        // Sequence options
        seqName_.setFont (uiFont());
        seqName_.setText (seq_.name, false);
        seqName_.onTextChange = [this] { seq_.name = seqName_.getText().trim(); changed (false); };
        addAndMakeVisible (seqName_);
        newButton_.onClick  = [this] { newSequence(); };
        loadButton_.onClick = [this] { loadSequence(); };
        saveButton_.onClick = [this] { saveSequence(); };
        for (auto* b : { &newButton_, &loadButton_, &saveButton_ }) addAndMakeVisible (b);

        loops_.setValue (seq_.loops);
        loops_.onValueChange = [this] (double v) { seq_.loops = (int) v; changed (false); };
        gap_.setValue (seq_.betweenStepsSec);
        gap_.onValueChange = [this] (double v) { seq_.betweenStepsSec = v; changed (false); };
        for (auto* f : { &loops_, &gap_ }) addAndMakeVisible (f);
        onError_.addItemList ({ "Stop the run", "Skip the step", "Retry once, then skip" }, 1);
        onError_.setSelectedId ((int) seq_.onError + 1, juce::dontSendNotification);
        onError_.onChange = [this] { seq_.onError = (rew::OnError) (onError_.getSelectedId() - 1); changed (false); };
        addAndMakeVisible (onError_);
        restore_.setToggleState (seq_.restoreRewSettings, juce::dontSendNotification);
        restore_.onClick = [this] { seq_.restoreRewSettings = restore_.getToggleState(); changed (false); };
        restore_.setTooltip ("Put REW's settings back as they were before the run");
        markers_.setToggleState (seq_.addLogMarkers, juce::dontSendNotification);
        markers_.onClick = [this] { seq_.addLogMarkers = markers_.getToggleState(); changed (false); };
        markers_.setTooltip ("One timeline marker per step at the start of its sweep");
        for (auto* b : { &restore_, &markers_ }) addAndMakeVisible (b);

        mdatButton_.onClick = [this] { chooseMdat(); };
        mdatClear_.onClick = [this] { seq_.saveMdatPath.clear(); changed (false); };
        resultsButton_.onClick = [this] { chooseResultsFolder(); };
        resultsClear_.onClick = [this] { seq_.resultsFolder.clear(); changed (false); };
        for (auto* b : { &mdatButton_, &mdatClear_, &resultsButton_, &resultsClear_ }) addAndMakeVisible (b);
        for (auto* l : { &mdatPath_, &resultsPath_, &lName_, &lLoops_, &lGap_, &lOnError_, &lMdat_, &lResults_, &issues_, &summary_ })
        {
            l->setFont (uiFont());
            addAndMakeVisible (l);
        }
        issues_.setJustificationType (juce::Justification::topLeft);
        summary_.setJustificationType (juce::Justification::centredRight);

        // Steps
        table_.onSelect = [this] (int row) { showStep (row); };
        table_.onToggleEnabled = [this] (int row)
        {
            if (row < 0 || row >= (int) seq_.steps.size()) return;
            seq_.steps[(size_t) row].enabled = ! seq_.steps[(size_t) row].enabled;
            changed (true);
        };
        addAndMakeVisible (table_);
        editor_.onChange = [this] { changed (false); editor_.setTitle (currentTitle()); };
        addAndMakeVisible (editor_);

        addButton_.onClick = [this] { addStep (false); };
        dupButton_.onClick = [this] { addStep (true); };
        posButton_.onClick = [this] { addPositions(); };
        delButton_.onClick = [this] { deleteStep(); };
        upButton_.onClick  = [this] { moveStep (-1); };
        downButton_.onClick = [this] { moveStep (1); };
        addButton_.setTooltip ("Add a step after the selected one (REW's current settings)");
        dupButton_.setTooltip ("Copy the selected step");
        posButton_.setTooltip ("Turn the selected step into N microphone positions that each wait for you");
        for (auto* b : { &addButton_, &dupButton_, &posButton_, &delButton_, &upButton_, &downButton_ })
            addAndMakeVisible (b);

        // Run (spec §4): the processor owns the run, this window only shows it
        runButton_.onClick = [this] { startRun(); };
        runButton_.setTooltip ("Run the sequence in REW");
        dryRun_.setTooltip ("Send every step's settings to REW, but do not start the sweeps");
        pauseButton_.onClick = [this]
        {
            auto& sq = processor_.getRewSequencer();
            sq.setPaused (! sq.isPaused());
        };
        pauseButton_.setTooltip ("Hold the run after the current step");
        abortButton_.onClick = [this] { processor_.getRewSequencer().abort(); };
        abortButton_.setTooltip ("Stop now: cancels the sweep in REW and restores REW's settings");
        continueButton_.onClick = [this] { processor_.getRewSequencer().continueAfterPrompt(); };
        skipButton_.onClick = [this] { processor_.getRewSequencer().skipPrompted(); };
        for (auto* c : std::initializer_list<juce::Component*> { &runButton_, &dryRun_, &pauseButton_, &abortButton_,
                                                                 &runStatus_, &progressBar_, &log_,
                                                                 &promptText_, &continueButton_, &skipButton_ })
            addAndMakeVisible (c);
        runStatus_.setFont (uiFont (14.0f));
        promptText_.setFont (uiFont (17.0f).boldened());
        promptText_.setJustificationType (juce::Justification::centred);
        log_.setMultiLine (true, false);
        log_.setReadOnly (true);
        log_.setScrollbarsShown (true);
        log_.setCaretVisible (false);
        log_.setFont (juce::Font (juce::FontOptions().withName (juce::Font::getDefaultMonospacedFontName()).withHeight (12.5f)));
        progressBar_.setPercentageDisplay (false);

        setLightMode (light);
        changed (true);
        if (! seq_.steps.empty())
            table_.selectRow (0);
        updateRunUi();   // a run may already be going (window reopened)
        startTimerHz (10);
    }

    ~RewSequencerComponent() override { poller_.stop(); }

    // The window calls this when it is shown or hidden: REW is only asked while open
    void setActive (bool active)
    {
        if (active) poller_.start (seq_.port);
        else        poller_.stop();
    }

    void setLightMode (bool light)
    {
        pal_ = Palette::get (light);
        table_.applyPalette (pal_);
        editor_.applyPalette (pal_);
        for (auto* t : { &seqName_ })
        {
            t->setColour (juce::TextEditor::backgroundColourId, pal_.field);
            t->setColour (juce::TextEditor::textColourId, pal_.text);
            t->setColour (juce::TextEditor::outlineColourId, pal_.border);
            t->setColour (juce::TextEditor::focusedOutlineColourId, pal_.accent);
            t->applyColourToAllText (pal_.text);
        }
        for (auto* f : { &port_, &loops_, &gap_ }) f->style (pal_);
        pal_.style (onError_);
        for (auto* b : { &restore_, &markers_ })
        {
            b->setColour (juce::ToggleButton::textColourId, pal_.text);
            b->setColour (juce::ToggleButton::tickColourId, pal_.accent);
            b->setColour (juce::ToggleButton::tickDisabledColourId, pal_.subtext);
        }
        for (auto* b : { &refresh_, &newButton_, &loadButton_, &saveButton_, &mdatButton_, &mdatClear_,
                         &resultsButton_, &resultsClear_, &addButton_, &dupButton_, &posButton_, &delButton_,
                         &upButton_, &downButton_ })
        {
            b->setColour (juce::TextButton::buttonColourId, pal_.field);
            b->setColour (juce::TextButton::textColourOffId, pal_.text);
        }
        for (auto* l : { &statusText_, &mdatPath_, &resultsPath_, &runStatus_, &promptText_ })
            l->setColour (juce::Label::textColourId, pal_.text);
        runButton_.setColour (juce::TextButton::buttonColourId, pal_.ok.withAlpha (0.85f));
        runButton_.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        abortButton_.setColour (juce::TextButton::buttonColourId, pal_.bad.withAlpha (0.85f));
        abortButton_.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        for (auto* b : { &pauseButton_, &skipButton_ })
        {
            b->setColour (juce::TextButton::buttonColourId, pal_.field);
            b->setColour (juce::TextButton::textColourOffId, pal_.text);
        }
        continueButton_.setColour (juce::TextButton::buttonColourId, pal_.accent);
        continueButton_.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        dryRun_.setColour (juce::ToggleButton::textColourId, pal_.text);
        dryRun_.setColour (juce::ToggleButton::tickColourId, pal_.accent);
        dryRun_.setColour (juce::ToggleButton::tickDisabledColourId, pal_.subtext);
        log_.setColour (juce::TextEditor::backgroundColourId, pal_.field.withAlpha (0.6f));
        log_.setColour (juce::TextEditor::textColourId, pal_.text);
        log_.setColour (juce::TextEditor::outlineColourId, pal_.border);
        log_.applyColourToAllText (pal_.text);
        progressBar_.setColour (juce::ProgressBar::backgroundColourId, pal_.field);
        progressBar_.setColour (juce::ProgressBar::foregroundColourId, pal_.ok);
        for (auto* l : { &portLabel_, &lName_, &lLoops_, &lGap_, &lOnError_, &lMdat_, &lResults_, &summary_ })
            l->setColour (juce::Label::textColourId, pal_.subtext);
        updateIssues();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (pal_.bg);
        for (auto r : { headerCard_, optionsCard_, runCard_ })
        {
            g.setColour (pal_.card);
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
            g.setColour (pal_.border);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
        }
        if (promptShown_)
        {
            g.setColour (pal_.accent.withAlpha (0.18f));
            g.fillRoundedRectangle (promptCard_.toFloat(), 6.0f);
            g.setColour (pal_.accent);
            g.drawRoundedRectangle (promptCard_.toFloat().reduced (0.5f), 6.0f, 1.5f);
        }
        // status dot
        g.setColour (statusColour_);
        g.fillEllipse (dot_.toFloat());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);

        headerCard_ = r.removeFromTop (40);
        {
            auto x = headerCard_.reduced (12, 8);
            dot_ = x.removeFromLeft (12).withSizeKeepingCentre (12, 12);
            x.removeFromLeft (8);
            refresh_.setBounds (x.removeFromRight (80));
            x.removeFromRight (6);
            port_.setBounds (x.removeFromRight (70));
            portLabel_.setBounds (x.removeFromRight (40));
            statusText_.setBounds (x);
        }
        r.removeFromTop (8);

        optionsCard_ = r.removeFromTop (100);
        {
            auto x = optionsCard_.reduced (12, 8);
            auto line1 = x.removeFromTop (28);
            lName_.setBounds (line1.removeFromLeft (72));
            saveButton_.setBounds (line1.removeFromRight (80).reduced (2, 1));
            loadButton_.setBounds (line1.removeFromRight (80).reduced (2, 1));
            newButton_.setBounds (line1.removeFromRight (64).reduced (2, 1));
            line1.removeFromRight (12);
            seqName_.setBounds (line1.reduced (0, 2));

            auto line2 = x.removeFromTop (28);
            lLoops_.setBounds (line2.removeFromLeft (72));
            loops_.setBounds (line2.removeFromLeft (56).reduced (0, 3));
            line2.removeFromLeft (16);
            lGap_.setBounds (line2.removeFromLeft (98));
            gap_.setBounds (line2.removeFromLeft (70).reduced (0, 3));
            line2.removeFromLeft (16);
            lOnError_.setBounds (line2.removeFromLeft (70));
            onError_.setBounds (line2.removeFromLeft (190).reduced (0, 2));
            line2.removeFromLeft (16);
            restore_.setBounds (line2.removeFromLeft (190));
            markers_.setBounds (line2.removeFromLeft (150));

            auto line3 = x.removeFromTop (28);
            auto half = line3.removeFromLeft (line3.getWidth() / 2);
            lMdat_.setBounds (half.removeFromLeft (72));
            mdatClear_.setBounds (half.removeFromRight (56).reduced (2, 2));
            mdatButton_.setBounds (half.removeFromRight (80).reduced (2, 2));
            mdatPath_.setBounds (half);
            line3.removeFromLeft (16);
            lResults_.setBounds (line3.removeFromLeft (64));
            resultsClear_.setBounds (line3.removeFromRight (56).reduced (2, 2));
            resultsButton_.setBounds (line3.removeFromRight (80).reduced (2, 2));
            resultsPath_.setBounds (line3);
        }
        r.removeFromTop (8);

        runCard_ = r.removeFromBottom (230);
        {
            auto x = runCard_.reduced (12, 10);
            auto line = x.removeFromTop (30);
            runButton_.setBounds (line.removeFromLeft (96).reduced (2, 1));
            dryRun_.setBounds (line.removeFromLeft (92).withTrimmedLeft (8));
            pauseButton_.setBounds (line.removeFromLeft (90).reduced (2, 1));
            abortButton_.setBounds (line.removeFromLeft (90).reduced (2, 1));
            line.removeFromLeft (12);
            runStatus_.setBounds (line);
            x.removeFromTop (6);
            progressBar_.setBounds (x.removeFromTop (10));
            x.removeFromTop (8);
            log_.setBounds (x);
            promptCard_ = x;
            auto pc = x.reduced (16, 12);
            auto buttons = pc.removeFromBottom (34).withSizeKeepingCentre (260, 34);
            continueButton_.setBounds (buttons.removeFromLeft (150).reduced (2, 0));
            skipButton_.setBounds (buttons.reduced (2, 0));
            promptText_.setBounds (pc);
        }
        r.removeFromBottom (8);

        auto bottom = r.removeFromBottom (44);
        summary_.setBounds (bottom.removeFromRight (220).removeFromTop (22));
        issues_.setBounds (bottom);
        r.removeFromBottom (6);

        editor_.setBounds (r.removeFromRight (440));
        r.removeFromRight (8);
        auto buttons = r.removeFromBottom (30);
        for (auto* b : { &addButton_, &dupButton_, &posButton_, &delButton_ })
        {
            b->setBounds (buttons.removeFromLeft (b == &posButton_ ? 124 : 90).reduced (2, 2));
        }
        downButton_.setBounds (buttons.removeFromRight (36).reduced (2, 2));
        upButton_.setBounds (buttons.removeFromRight (36).reduced (2, 2));
        r.removeFromBottom (6);
        table_.setBounds (r);
    }

private:
    //==========================================================================
    void timerCallback() override
    {
        updateRunUi();
        const auto st = poller_.state();
        if (st.serial == shownSerial_)
            return;
        shownSerial_ = st.serial;
        choices_ = st.choices;

        juce::String text;
        if (! st.checked)
        {
            text = U ("Looking for REW…");
            statusColour_ = pal_.subtext;
        }
        else if (! st.reachable)
        {
            text = "REW not reachable on port " + juce::String (seq_.port)
                   + U (". Start REW and enable its API (Preferences → API).");
            statusColour_ = pal_.bad;
        }
        else
        {
            const auto driver = st.choices.driver == rew::Driver::Asio ? "ASIO" : "Java";
            text = "REW " + st.version.rew + U (" · API ") + st.version.api + U (" · ") + driver;
            if (st.choices.sampleRate > 0.0)
            {
                const double khz = st.choices.sampleRate / 1000.0;
                text << U (" · ") << juce::String (khz, std::abs (khz - std::round (khz)) < 1.0e-9 ? 0 : 1) << " kHz";
            }
            if (! st.audioReady)
                text << U (" · audio not ready");
            statusColour_ = st.audioReady ? pal_.ok : pal_.warn;
        }
        statusText_.setText (text, juce::dontSendNotification);
        statusText_.setTooltip (st.blocking ? "REW's blocking mode is on; a run switches it off and back on afterwards."
                                            : juce::String());
        showStep (table_.selectedRow());   // choice lists may have changed
        updateIssues();
        repaint();
    }

    //==========================================================================
    // Any edit: store in the processor, refresh table and validation
    void changed (bool structural)
    {
        processor_.setRewSequence (seq_);
        updateIssues();
        if (structural)
            showStep (table_.selectedRow());
        mdatPath_.setText (seq_.saveMdatPath.isNotEmpty() ? seq_.saveMdatPath : juce::String ("not saved"),
                           juce::dontSendNotification);
        mdatPath_.setTooltip (seq_.saveMdatPath);
        resultsPath_.setText (seq_.resultsFolder.isNotEmpty() ? seq_.resultsFolder
                                                              : juce::String ("Documents/SPLMeter/REW runs"),
                              juce::dontSendNotification);
        resultsPath_.setTooltip (seq_.resultsFolder);
    }

    void updateIssues()
    {
        const auto issues = rew::validate (seq_, choices_);
        std::vector<bool> errorRows (seq_.steps.size(), false);
        juce::StringArray lines;
        int errors = 0;
        for (const auto& is : issues)
        {
            if (is.error && is.step >= 0)
                errorRows[(size_t) is.step] = true;
            errors += is.error ? 1 : 0;
            if (lines.size() < 3)
                lines.add ((is.error ? U ("✕ ") : U ("⚠ "))
                           + (is.step >= 0 ? rew::stepTitle (seq_, is.step) + ": " : juce::String()) + is.message);
        }
        if ((int) issues.size() > 3)
            lines.add (U ("… and ") + juce::String ((int) issues.size() - 3) + " more");
        if (issues.empty())
            lines.add (choices_.fromRew ? U ("✓ The sequence fits this REW setup.")
                                        : U ("✓ No problems found. Outputs and inputs are checked once REW is connected."));
        hasErrors_ = errors > 0;
        issues_.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
        issues_.setColour (juce::Label::textColourId, errors > 0 ? pal_.bad : (issues.empty() ? pal_.ok : pal_.warn));

        const int sweeps = rew::totalSweeps (seq_);
        summary_.setText (juce::String ((int) seq_.steps.size()) + " steps" + U (" · ") + juce::String (sweeps)
                          + (sweeps == 1 ? " sweep" : " sweeps"), juce::dontSendNotification);
        table_.setSequence (&seq_, std::move (errorRows));
    }

    juce::String currentTitle() const
    {
        const int row = table_.selectedRow();
        return row >= 0 && row < (int) seq_.steps.size() ? rew::stepTitle (seq_, row) : juce::String();
    }

    void showStep (int row)
    {
        if (row >= 0 && row < (int) seq_.steps.size())
            editor_.setStep (&seq_.steps[(size_t) row], rew::stepTitle (seq_, row), choices_);
        else
            editor_.setStep (nullptr, {}, choices_);
    }

    //==========================================================================
    // Run (spec §4, §5)
    void startRun()
    {
        processor_.setRewSequence (seq_);
        if (! processor_.startRewRun (dryRun_.getToggleState()))
            return;
        shownResults_ = -1;
        updateRunUi();
    }

    static juce::String mmss (double sec)
    {
        const int s = juce::jmax (0, (int) std::round (sec));
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }

    void updateRunUi()
    {
        auto& sq = processor_.getRewSequencer();
        const auto p = sq.progress();
        const bool running = p.running();

        if (running != wasRunning_)
        {
            wasRunning_ = running;
            setEditing (! running);
            if (! running)
                poller_.refreshNow();
        }
        runButton_.setEnabled (! running && ! hasErrors_ && ! seq_.steps.empty());
        dryRun_.setEnabled (! running);
        pauseButton_.setEnabled (running);
        pauseButton_.setButtonText (sq.isPaused() ? "Resume" : "Pause");
        abortButton_.setEnabled (running);
        table_.setActiveRow (running ? p.step : -1);

        // Position among the enabled steps
        int enabledIndex = 0, enabledCount = 0;
        for (int i = 0; i < (int) seq_.steps.size(); ++i)
            if (seq_.steps[(size_t) i].enabled)
            {
                ++enabledCount;
                if (i <= p.step) enabledIndex = enabledCount;
            }

        juce::String status;
        double fraction = 0.0;
        if (p.totalSweeps > 0)
        {
            double inStep = 0.0;
            if (p.state == rew::RunState::Measuring && p.stepExpectedSec > 0.0)
                inStep = juce::jlimit (0.0, 0.95, p.stepElapsedSec / p.stepExpectedSec);
            fraction = juce::jlimit (0.0, 1.0, (p.sweepsDone + inStep) / p.totalSweeps);
        }
        if (running)
        {
            status = (p.dryRun ? "Dry run" + U (" · ") : juce::String());
            if (p.step >= 0)
            {
                status << "Step " << enabledIndex << "/" << enabledCount;
                if (seq_.loops > 1)
                    status << U (" · loop ") << (p.loop + 1) << "/" << seq_.loops;
                status << U (" · ") << p.title << U (" · ");
            }
            status << rew::toString (p.state);
            if (p.state == rew::RunState::Measuring)
                status << " " << mmss (p.stepElapsedSec) << U (" / ~") << mmss (p.stepExpectedSec);
        }
        else if (p.state != rew::RunState::Idle)
        {
            int ok = 0, warn = 0, failed = 0, skipped = 0;
            for (const auto& r : p.results)
            {
                ok      += r.status == rew::StepStatus::Done ? 1 : 0;
                warn    += r.status == rew::StepStatus::Warning ? 1 : 0;
                failed  += r.status == rew::StepStatus::Failed ? 1 : 0;
                skipped += (r.status == rew::StepStatus::Skipped || r.status == rew::StepStatus::Cancelled) ? 1 : 0;
            }
            status << rew::toString (p.state) << ": " << ok << " ok";
            if (warn > 0)    status << ", " << warn << " with warnings";
            if (failed > 0)  status << ", " << failed << " failed";
            if (skipped > 0) status << ", " << skipped << " skipped";
            if (p.state == rew::RunState::Done) fraction = 1.0;
        }
        else
        {
            status = enabledCount > 0 ? "Ready" : "Add steps to run";
        }
        runStatus_.setText (status, juce::dontSendNotification);
        progressValue_ = fraction;

        // Prompt banner (wait for user)
        const bool prompting = p.state == rew::RunState::WaitingForUser;
        promptText_.setVisible (prompting);
        continueButton_.setVisible (prompting);
        skipButton_.setVisible (prompting);
        log_.setVisible (! prompting);
        if (prompting != promptShown_)
        {
            promptShown_ = prompting;
            if (prompting)
                if (auto* top = getTopLevelComponent())
                    top->toFront (true);
            repaint();
        }
        if (prompting)
            promptText_.setText (p.prompt, juce::dontSendNotification);

        // Run log: rebuilt when a result or the final message arrives
        const int key = (int) p.results.size() * 4 + (running ? 0 : 1) + (p.message.isNotEmpty() ? 2 : 0);
        if (key != shownResults_)
        {
            shownResults_ = key;
            log_.setText (logText (p), false);
            log_.moveCaretToEnd();
        }
    }

    juce::String logText (const rew::Progress& p) const
    {
        juce::StringArray lines;
        if (p.version.valid())
            lines.add ("REW " + p.version.rew + U (" · API ") + p.version.api + (p.dryRun ? U (" · dry run") : juce::String()));
        for (const auto& r : p.results)
        {
            juce::String line;
            line << (r.start != juce::Time() ? r.start.toString (false, true, true, true) : juce::String ("--:--:--"))
                 << "  #" << juce::String (r.step + 1).paddedRight (' ', 3)
                 << (r.loop > 0 || seq_.loops > 1 ? "L" + juce::String (r.loop + 1) + "  " : juce::String())
                 << r.title.paddedRight (' ', 20) << "  " << juce::String (rew::toString (r.status)).paddedRight (' ', 9);
            if (r.uuids.size() > 0)
                line << "  " << r.uuids.size() << (r.uuids.size() == 1 ? " measurement" : " measurements")
                     << " (" << juce::String (r.durationSec, 1) << " s)";
            if (r.attempts > 1)
                line << U ("  · retried");
            for (const auto& m : r.messages)
                line << "\n            " << m;
            lines.add (line);
        }
        if (p.savedMdat.isNotEmpty())
            lines.add ("Saved REW measurements: " + p.savedMdat);
        if (p.message.isNotEmpty())
            lines.add (p.message);
        return lines.joinIntoString ("\n");
    }

    void setEditing (bool on)
    {
        for (auto* c : std::initializer_list<juce::Component*> {
                 &table_, &editor_, &seqName_, &newButton_, &loadButton_, &port_, &loops_, &gap_, &onError_,
                 &restore_, &markers_, &mdatButton_, &mdatClear_, &resultsButton_, &resultsClear_,
                 &addButton_, &dupButton_, &posButton_, &delButton_, &upButton_, &downButton_ })
        {
            c->setEnabled (on);
            c->setAlpha (on ? 1.0f : 0.55f);
        }
    }

    //==========================================================================
    void addStep (bool duplicate)
    {
        if ((int) seq_.steps.size() >= rew::limits::kMaxSteps)
            return;
        const int sel = table_.selectedRow();
        rew::Step s;
        if (duplicate && sel >= 0)
            s = seq_.steps[(size_t) sel];
        const int at = sel >= 0 ? sel + 1 : (int) seq_.steps.size();
        seq_.steps.insert (seq_.steps.begin() + at, s);
        changed (true);
        table_.selectRow (at);
    }

    void deleteStep()
    {
        const int sel = table_.selectedRow();
        if (sel < 0)
            return;
        seq_.steps.erase (seq_.steps.begin() + sel);
        changed (true);
        table_.selectRow (std::min (sel, (int) seq_.steps.size() - 1));
        showStep (table_.selectedRow());
    }

    void moveStep (int delta)
    {
        const int sel = table_.selectedRow();
        if (sel < 0 || sel + delta < 0 || sel + delta >= (int) seq_.steps.size())
            return;
        rew::moveStep (seq_, sel, delta);
        changed (true);
        table_.selectRow (sel + delta);
    }

    void addPositions()
    {
        const int sel = table_.selectedRow();
        if (sel < 0)
            return;
        auto* w = new juce::AlertWindow ("Add positions",
                                         "The selected step becomes position 1; copies for the other positions follow it. "
                                         "Each one waits for you to move the microphone.",
                                         juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("n", "5", "Number of positions:");
        w->addButton ("Add", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<RewSequencerComponent> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, sel] (int result)
        {
            std::unique_ptr<juce::AlertWindow> owner (w);   // deleted here, after reading it
            const int n = juce::jlimit (1, 99, w->getTextEditorContents ("n").getIntValue());
            if (result == 1 && safe != nullptr)
            {
                rew::addPositions (safe->seq_, sel, n);
                safe->changed (true);
                safe->table_.selectRow (sel);
            }
        }), false);
    }

    //==========================================================================
    void newSequence()
    {
        juce::Component::SafePointer<RewSequencerComponent> safe (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "New sequence",
                                            "Start a new, empty sequence? Unsaved steps are lost.",
                                            "New", "Cancel", this,
                                            juce::ModalCallbackFunction::create ([safe] (int ok)
        {
            if (ok == 0 || safe == nullptr) return;
            rew::Sequence fresh;
            fresh.port = safe->seq_.port;
            safe->setSequence (fresh);
        }));
    }

    void setSequence (const rew::Sequence& q)
    {
        seq_ = q;
        seqName_.setText (seq_.name, false);
        port_.setValue (seq_.port);
        loops_.setValue (seq_.loops);
        gap_.setValue (seq_.betweenStepsSec);
        onError_.setSelectedId ((int) seq_.onError + 1, juce::dontSendNotification);
        restore_.setToggleState (seq_.restoreRewSettings, juce::dontSendNotification);
        markers_.setToggleState (seq_.addLogMarkers, juce::dontSendNotification);
        poller_.setPort (seq_.port);
        table_.selectRow (-1);
        changed (true);
        if (! seq_.steps.empty())
            table_.selectRow (0);
    }

    void loadSequence()
    {
        chooser_ = std::make_unique<juce::FileChooser> ("Load REW sequence", UserPrefs::lastUsedFolder(), "*.rewseq.json");
        juce::Component::SafePointer<RewSequencerComponent> safe (this);
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [safe] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (safe == nullptr || f == juce::File())
                return;
            rew::Sequence q;
            juce::String error;
            if (! rew::loadFromFile (f, q, error))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Load REW sequence", f.getFileName() + ": " + error);
                return;
            }
            UserPrefs::saveLastUsedFolder (f);
            safe->setSequence (q);
        });
    }

    void saveSequence()
    {
        const auto name = juce::File::createLegalFileName (seq_.name.isNotEmpty() ? seq_.name : juce::String ("Sequence"));
        chooser_ = std::make_unique<juce::FileChooser> ("Save REW sequence",
                                                        UserPrefs::lastUsedFolder().getChildFile (name + ".rewseq.json"),
                                                        "*.rewseq.json");
        juce::Component::SafePointer<RewSequencerComponent> safe (this);
        chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                               [safe] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (safe == nullptr || f == juce::File())
                return;
            if (! f.getFileName().endsWithIgnoreCase (".rewseq.json"))
                f = f.getSiblingFile (f.getFileNameWithoutExtension() + ".rewseq.json");
            if (! rew::saveToFile (safe->seq_, f))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Save REW sequence", "Could not write " + f.getFullPathName());
            else
                UserPrefs::saveLastUsedFolder (f);
        });
    }

    void chooseMdat()
    {
        const auto start = seq_.saveMdatPath.isNotEmpty() ? juce::File (seq_.saveMdatPath)
                                                          : UserPrefs::lastUsedFolder().getChildFile (
                                                                juce::File::createLegalFileName (seq_.name) + ".mdat");
        chooser_ = std::make_unique<juce::FileChooser> ("REW measurements file (Save all at the end of the run)",
                                                        start, "*.mdat");
        juce::Component::SafePointer<RewSequencerComponent> safe (this);
        chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                               [safe] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (safe == nullptr || f == juce::File())
                return;
            safe->seq_.saveMdatPath = f.withFileExtension ("mdat").getFullPathName();
            safe->changed (false);
        });
    }

    void chooseResultsFolder()
    {
        const auto start = seq_.resultsFolder.isNotEmpty() ? juce::File (seq_.resultsFolder)
                                                           : UserPrefs::lastUsedFolder();
        chooser_ = std::make_unique<juce::FileChooser> ("Folder for the run results (CSV, JSON)", start);
        juce::Component::SafePointer<RewSequencerComponent> safe (this);
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                               [safe] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (safe == nullptr || f == juce::File())
                return;
            safe->seq_.resultsFolder = f.getFullPathName();
            safe->changed (false);
        });
    }

    //==========================================================================
    SPLMeterAudioProcessor& processor_;
    rew::Sequence seq_;
    rew::Choices choices_ = rew::offlineChoices();
    StatusPoller poller_;
    int shownSerial_ = -1;
    Palette pal_ = Palette::get (false);
    juce::Colour statusColour_ = juce::Colours::grey;
    juce::Rectangle<int> headerCard_, optionsCard_, dot_;
    std::unique_ptr<juce::FileChooser> chooser_;

    juce::Label statusText_ { {}, U ("Looking for REW…") }, portLabel_ { {}, "Port" };
    NumberField port_ { 1024.0, 65535.0, 0 };
    juce::TextButton refresh_ { "Refresh" };

    juce::Label lName_ { {}, "Sequence" }, lLoops_ { {}, "Loops" }, lGap_ { {}, "Gap between" },
                lOnError_ { {}, "On error" }, lMdat_ { {}, "REW file" }, lResults_ { {}, "Results" };
    juce::TextEditor seqName_;
    juce::TextButton newButton_ { "New" }, loadButton_ { U ("Load…") }, saveButton_ { U ("Save…") };
    NumberField loops_ { 1.0, rew::limits::kMaxLoops, 0 }, gap_ { 0.0, rew::limits::kMaxDelaySec, 1, "s" };
    juce::ComboBox onError_;
    juce::ToggleButton restore_ { "Restore REW settings" }, markers_ { "Timeline markers" };
    juce::Label mdatPath_, resultsPath_;
    juce::TextButton mdatButton_ { U ("Choose…") }, mdatClear_ { "Clear" },
                     resultsButton_ { U ("Choose…") }, resultsClear_ { "Default" };

    StepTable table_;
    StepEditor editor_;
    juce::TextButton addButton_ { "+ Add" }, dupButton_ { "Duplicate" }, posButton_ { U ("Add positions…") },
                     delButton_ { "Delete" }, upButton_ { U ("▲") }, downButton_ { U ("▼") };
    juce::Label issues_, summary_;
    bool hasErrors_ = false;

    juce::Rectangle<int> runCard_, promptCard_;
    juce::TextButton runButton_ { U ("▶  Run") }, pauseButton_ { "Pause" }, abortButton_ { U ("■  Abort") },
                     continueButton_ { "Continue" }, skipButton_ { "Skip step" };
    juce::ToggleButton dryRun_ { "Dry run" };
    juce::Label runStatus_, promptText_;
    double progressValue_ = 0.0;
    juce::ProgressBar progressBar_ { progressValue_ };
    juce::TextEditor log_;
    bool wasRunning_ = false, promptShown_ = false;
    int shownResults_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RewSequencerComponent)
};

//==============================================================================
class RewSequencerWindow : public juce::DocumentWindow
{
public:
    RewSequencerWindow (SPLMeterAudioProcessor& p, bool light)
        : juce::DocumentWindow ("REW Measurement Sequencer", juce::Colour (0xff1c1c1e), juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (false);
        content_ = new RewSequencerComponent (p, light);
        content_->setSize (1180, 940);
        setContentOwned (content_, true);
        setResizable (true, false);
        setResizeLimits (1060, 840, 2000, 1600);
    }

    void setLightMode (bool light) { content_->setLightMode (light); }
    void closeButtonPressed() override { setVisible (false); }
    void visibilityChanged() override { content_->setActive (isVisible()); }

private:
    RewSequencerComponent* content_ = nullptr;   // owned by the window
    juce::TooltipWindow tooltipWindow_ { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RewSequencerWindow)
};
} // namespace RewUi
