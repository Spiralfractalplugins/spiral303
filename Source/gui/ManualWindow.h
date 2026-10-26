#pragma once

#include <vector>

#include <JuceHeader.h>

#include "DarkLookAndFeel.h"

namespace
{
    /** juce::String (const char*) reads raw bytes (ASCII by design), so every
        literal with non-ASCII text must come through here. */
    inline juce::String U8 (const char* s) { return juce::String::fromUTF8 (s); }
}

/** The in-plugin user manual: a scrollable dark page that mirrors the theme of the
    editor. Text is wrapped with a plain word wrapper (Graphics::drawText per line),
    headings carry the colours of the panels they describe, and a toggle switches
    the language between RU and EN. */
class ManualContent : public juce::Component
{
public:
    ManualContent()
    {
        addAndMakeVisible (langButton);
        langButton.onClick = [this] { setRussian (! russian); };
        russian = readStoredLanguage();
        langButton.setButtonText (russian ? "EN" : "RU");
    }

    void setRussian (bool ru)
    {
        russian = ru;
        writeStoredLanguage (russian);
        langButton.setButtonText (russian ? "EN" : "RU");
        relayout (jmax (100, layoutWidth));
        // relayout may keep the same height, in which case setSize does not
        // generate a paint event and the old page stays on screen.
        repaint();
    }

    /** The click handler of the language button; public so tests can drive it
        synchronously (Button::triggerClick dispatches asynchronously). */
    void toggleLanguage() { setRussian (! russian); }

    /** Lays the document out for the given width and sizes the component to fit;
        called by the viewport on resize and by tests to paint offscreen. */
    void relayout (int widthPx)
    {
        if (widthPx < 100)
            return;
        layoutWidth = widthPx;
        messageWidth = widthPx - 2 * margin;

        buildLines();
        setSize (widthPx, juce::jlimit (160, 8000, layoutBottom + 12));
    }

    void resized() override
    {
        const int w = getWidth();
        if (w != layoutWidth && w > 100)
            relayout (w);
        langButton.setBounds (w - margin - 46, 8, 46, 24);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& th = Skins::current();
        g.fillAll (th.background);

        // Header band matching the editor: amber rule under the title row.
        g.setColour (th.accent);
        g.setFont (Theme::font (20.0f, juce::Font::bold));
        g.drawText (U8 (russian ? "Руководство пользователя" : "User manual"),
                    juce::Rectangle<int> (margin, 4, messageWidth, 30),
                    juce::Justification::centredLeft);
        g.setColour (th.panelOutline);
        g.fillRect (margin, 36, messageWidth, 1);

        const auto bodyFont = Theme::font (13.0f);
        int y = 44;
        for (const auto& line : lines)
        {
            if (y + 20 >= getHeight()) break;
            if (line.kind == 0)
            {
                g.setColour (Skins::ink (th, line.role));
                g.setFont (Theme::font (15.0f, juce::Font::bold));
                g.drawText (line.text, margin, y, messageWidth, 20,
                            juce::Justification::centredLeft, false);
                y += 28;
            }
            else
            {
                g.setColour (Skins::ink (th, line.role));
                g.setFont (bodyFont);
                g.drawText (line.text, margin, y, messageWidth, 18,
                            juce::Justification::centredLeft, false);
                y += 19;
            }
        }
    }

private:
    struct Section
    {
        const char* ru;
        const char* en;
        InkRole role;
        const char* ruBody;
        const char* enBody;
    };

    /** A role rather than a colour: the lines are wrapped once and kept, while
        the skin can change between the wrap and the paint. */
    struct Line
    {
        juce::String text;
        int kind = 1;            // 0 heading, 1 body
        InkRole role { InkRole::Text };
    };

    static const std::vector<Section>& sections()
    {
        static const std::vector<Section> s = {
            { "Секвенсор", "Sequencer", InkRole::Pitch,
                "Паттерн до 32 шагов. ЛКМ по шагу ставит или снимает ноту, ПКМ открывает "
                "меню шага: выбор ноты по гамме и флажки TIE (держать предыдущую ноту), "
                "SL (слайд) и AC (акцент). Нижние ряды TIE / SL / AC тоже рисуются мышью. "
                "Длина паттерна: 8 / 12 / 16 / 24 / 32 шагов (список STEPS).\n"
                "NOTE и SCALE задают базовую ноту и одну из 12 гамм. Все правки "
                "применяются на лету, без остановки игры.\n"
                "Velocity каждой ноты: потяните ноту вертикально — вверх громче, вниз "
                "тише; яркость полоски показывает уровень. Наведение мыши читает число "
                "в процентах.",
                "Up to 32 steps. Left-click toggles a note on a step; right-click opens the "
                "step menu with the pitch in the scale plus the TIE (hold the previous "
                "note), SL (slide) and AC (accent) flags. The lower TIE / SL / AC rows can "
                "be painted too. Pattern length: 8 / 12 / 16 / 24 / 32 steps (STEPS).\n"
                "NOTE and SCALE set the root note and one of 12 scales. Every edit is "
                "applied on the fly while the sequencer keeps running.\n"
                "Per-note velocity: drag a note vertically — louder up, quieter down; the "
                "bar's brightness shows the level. Hovering reads the number in per cent." },

            { "Гейт-колесо", "Gate wheel", InkRole::Gate,
                "16 секторов вокруг круга: тёмно-красные — ноты приглушены. Клики "
                "включают/выключают секторы, список GATE DIV делит круг на "
                "1/2/4/8/16 фигур, поле ROTATION поворачивает маску. В пресетах "
                "хранится как g1:<HEX>:<rotation>. Кнопка ON пропускает эффект целиком.",
                "16 sectors around the circle: dark red ones mute the note while the gate "
                "is over them. Clicks toggle sectors, the GATE DIV selector splits the "
                "circle into 1/2/4/8/16 figures, and the ROTATION field rotates the mask. "
                "Presets store it as g1:<HEX>:<rotation>. The ON button bypasses the "
                "whole effect." },

            { "Голос и фильтр", "Voice and filter", InkRole::Accent,
                "WAVE — морф синус → пила → квадрат. CUTOFF — частота среза, которую "
                "огибающая открывает на каждой ноте (40–8000 Гц). Res — резонанс. "
                "Env — глубина открытия фильтра огибающей.\n"
                "FILTER (селектор под ручкой Cutoff) — модель фильтра: Classic (SVF), "
                "Diode Ladder, Moog Ladder, TPT SVF, Sallen-Key, One-Pole, Vowel, Diode 303 (асимметричный диод: вторая гармоника, резонанс садится с ростом среза).",
                "WAVE morphs sine → saw → square. CUTOFF is the corner the contour "
                "envelope opens on every note (40–8000 Hz). Res is the resonance. "
                "Env is the squelch depth.\n"
                "FILTER (the selector under the Cutoff knob) picks the lowpass model: "
                "Classic (SVF), Diode Ladder, Moog Ladder, TPT SVF, Sallen-Key, One-Pole, Vowel, Diode 303 (asymmetric diode pair: a real second harmonic, and the resonance sags as the cutoff climbs)." },

            { "Огибающая и аналог", "Envelope and analog", InkRole::AccentAlt,
                "Contour — длина огибающей среза как доля шага. Accent — прибавка "
                "уровня и открытия фильтра на AC-шагах; подряд идущие AC заряжают акцент "
                "сильнее (до +75% на четвёртом), пауза или обычная нота сбрасывают цепочку. Glide — длина слайда на "
                "SL-шагах. Warmth — сила аналогового дрейфа: высота тона, срез, "
                "уровень и Q фильтра плавно плавают.",
                "Contour is the decay length of the filter envelope as a fraction of a "
                "step. Accent raises the level and filter opening of AC steps; consecutive AC "
                "steps charge it further (up to +75% at the fourth), and a rest or a plain note "
                "ends the chain. Glide is "
                "the slide length on SL steps. Warmth drives the analog drift: pitch, "
                "cutoff, level and the filter Q slowly wander." },

            { "Дисторшн и выход", "Distortion and output", InkRole::Pitch,
                "Drive — вход танх-клиппера (уровень голоса внутри фиксирован на "
                "полном заводском), Dist Mix — сухая/мокрая смесь. В ряду "
                "сатуратора выбирается тип: Tanh, Drive, Triode, Fold, Master Clip, "
                "Tape, Tape Warm, Cassette, Reel, Tape Hot и Clean (точный bypass). "
                "Gain — уровень мастера.",
                "Drive pushes the input of the tanh clipper (the voice level sits at "
                "the full factory setting inside), Dist Mix blends it in. The "
                "saturator row picks the type: Tanh, Drive, Triode, Fold, Master Clip, "
                "Tape, Tape Warm, Cassette, Reel, Tape Hot and Clean (an exact bypass). "
                "Gain is the master level." },

            { "Транспорт", "Transport", InkRole::AccentAlt,
                "SYNC = HOST — темп и позиция берутся у хоста, ручка BPM серая и "
                "зеркалит темп хоста. SYNC = MANUAL — работают ручка BPM и кнопка "
                "PLAY. Swing сдвигает каждый второй шаг, CLOCK — делитель частоты "
                "шагов.\n"
                "Кнопка SEQ: выключена — плагин живёт как обычный инструмент "
                "секвенсора не запускается, ноты с клавиатуры играются "
                "напрямую (и при работающем транспорте, и на паузе). Включена — "
                "сетка играет как обычно, в обоих режимах Sync.",
                "SYNC = HOST follows the host tempo and play head; the BPM knob greys "
                "out and mirrors the host tempo. SYNC = MANUAL enables the BPM knob and "
                "the PLAY button. Swing delays every second step; CLOCK divides the "
                "step rate.\n"
                "The SEQ button: off, the plugin behaves as a plain instrument - the "
                "internal sequencer stands down and keyboard notes sound directly "
                "(with the host transport rolling or parked). On, the grid plays as "
                "usual, in either sync mode." },

            { "Эффекты", "Effects", InkRole::Pitch,
                "Строка FX под скоупами. DIV — длина повтора в долях от темпа: четверть, "
                "восьмая, восьмая с точкой, шестнадцатая, восьмая триоль. DEL MIX — доля "
                "дилэя (0 — стадия выключена целиком). FBK — обратная связь, не выше 0.9. "
                "TONE — ФНЧ в петле: тёплые повторы около 3 кГц при 0.65. REV MIX — посыл "
                "в короткую комнату, DECAY — её затухание. Оба mix по умолчанию 0, поэтому "
                "старые пресеты звучат как прежде.",
                "The FX strip under the scopes. DIV sets the repeat length in beats of "
                "the tempo: quarter, eighth, dotted eighth, sixteenth, eighth triplet. "
                "DEL MIX is the delay's dry/wet (0 bypasses the stage entirely). FBK is "
                "the feedback, capped at 0.9. TONE is the lowpass in the loop: warm "
                "repeats near 3 kHz at 0.65. REV MIX is the send into a short room, "
                "DECAY its decay. Both mixes default to 0, so older presets sound as before." },

            { "Оверсэмплинг", "Oversampling", InkRole::Pitch,
                "Выбор ОС 2x / ОС 4x включает оверсэмплинг только сатурации: сигнал "
                "поднимается в 2 или 4 раза, прогоняется через сатурацию и "
                "возвращается на исходную частоту. Резонансный фильтр и остальная "
                "цепочка работают как обычно. По умолчанию выключено. На слух "
                "убирает звон при высоком драйве; на низких драйвах разницы почти нет.",
                "OS 2x / OS 4x oversample the saturation only: the signal is lifted "
                "to two or four times the host rate, shaped there, and brought back "
                "down. The resonant filter and the rest of the chain stay at the "
                "host rate. Off by default. Audible mainly at high drive, where it "
                "removes the aliasing ring; at low drive the difference is small." },

            { "Лимитер", "Limiter", InkRole::Gate,
                "Мастер-лимитер в конце цепи: порог (-20..0 дБ) и потолок (-6..0 дБ). "
                "Колено мягкое: снижение громкости нарастает плавно вокруг порога, а не "
                "скачком. Под потолком - клиппер, который не даёт сигналу выскочить выше "
                "потолка и не имеет скачка на границе. Всё это держит пиковые всплески, "
                "а не меняет звук на малых уровнях.",
                "Master limiter at the end of the chain: threshold (-20..0 dB) and ceiling "
                "(-6..0 dB). The knee is soft: gain reduction ramps in around the threshold "
                "instead of switching on at a corner. Under the ceiling sits a clipper that "
                "never lets the signal past it and has no step at the boundary. It holds "
                "peaks in place without touching the sound at low levels." },

            { "Пресеты", "Presets", InkRole::Gate,
                "17 фабричных программ в меню Init. «Save current settings as preset…» "
                "сохраняет текущий звук в %APPDATA%/Spiral303/Presets; перетащите "
                ".json на окно — пресет загрузится. Пользовательские пресеты удаляются "
                "из того же меню.",
                "17 factory programs in the Init menu. Save current settings as preset… "
                "writes the current sound to %APPDATA%/Spiral303/Presets; drop a .json "
                "file onto the window to load one. User presets can be deleted from the "
                "same menu." },
        };
        return s;
    }

    /** Word-wraps the whole document into per-line draw calls. */
    void buildLines()
    {
        lines.clear();
        layoutBottom = 44;

        auto wrapBody = [this] (const juce::String& body, std::vector<Line>& out)
        {
            const auto font = Theme::font (13.0f);
            const float limit = static_cast<float> (messageWidth);
            const auto paragraphs = juce::StringArray::fromTokens (body, "\n", "");

            for (const auto& paragraph : paragraphs)
            {
                juce::String current;                    // line being built
                const auto words = juce::StringArray::fromTokens (paragraph, " ", "");
                for (const auto& word : words)
                {
                    juce::String candidate = current;
                    if (candidate.isNotEmpty())
                        candidate += " ";
                    candidate += word;
                    if (Theme::textWidth (font, candidate) > limit && current.isNotEmpty())
                    {
                        out.push_back ({ current, 1, InkRole::Text });
                        current = word;
                    }
                    else
                        current = candidate;
                }
                out.push_back ({ current, 1, InkRole::Text });
            }
        };

        // Title two-liner.
        lines.push_back ({ U8 (russian ? "Эмуляция Roland TB-303" : "A Roland TB-303 emulation"),
                           1, InkRole::Text });
        lines.push_back ({ russian
                ? U8 ("Секвенсор, морфирующий осциллятор, резонансный фильтр, дисторшн и "
                      "аналоговый дрейф.  Версия " SPIRAL303_VERSION)
                : juce::String ("Morphing oscillator, resonant filter, distortion, "
                                "sequencer and analog drift.  Version " SPIRAL303_VERSION),
            1, InkRole::TextDim });
        lines.push_back ({ {}, 1, InkRole::TextDim });

        for (const auto& s : sections())
        {
            Line heading { U8 (russian ? s.ru : s.en), 0, s.role };
            lines.push_back (heading);
            wrapBody (U8 (russian ? s.ruBody : s.enBody), lines);
            lines.push_back ({ {}, 1, InkRole::Text });
        }

        lines.push_back ({ "Spiralfractal / Konstantin Chizhov  -  konstantin.qi@gmail.com",
                           1, InkRole::TextDim });

        for (const auto& line : lines)
            layoutBottom += line.kind == 0 ? 28 : (line.text.isNotEmpty() ? 19 : 12);
    }

    bool russian = false;
    int layoutWidth = 0, messageWidth = 0, layoutBottom = 44;
    std::vector<Line> lines;
    juce::TextButton langButton { "RU" };

    static constexpr int margin = 14;

    /** The chosen language remembers across openings: a process-wide static
        survives the dialog being closed and recreated. */
    static bool readStoredLanguage()
    {
        return storedLanguage();
    }

    static void writeStoredLanguage (bool ru)
    {
        storedLanguage() = ru;
    }

    static bool& storedLanguage()
    {
        static bool value = false;
        return value;
    }
};

/** Scrolls a ManualContent inside a fixed-size viewport. */
class ManualView : public juce::Viewport
{
public:
    ManualView()
    {
        setViewedComponent (new ManualContent(), true);
        setScrollBarsShown (true, false);
    }

    void reflow()
    {
        if (auto* content = static_cast<ManualContent*> (getViewedComponent()))
            content->relayout (jmax (160, getMaximumVisibleWidth()));
    }

    void resized() override { reflow(); }

    void parentHierarchyChanged() override
    {
        // The dialog sized this viewport while there was no window yet, so the
        // first reflow happens on attach, not on a user resize alone.
        reflow();
    }
};

/** Opens the manual as a non-modal dialog; repeated calls focus the existing one. */
inline void showManualWindow()
{
    static juce::Component::SafePointer<juce::DialogWindow> dialog;
    if (dialog != nullptr)
    {
        dialog->toFront (true);
        return;
    }

    // Size to the screen: tall enough for the whole page, wide enough for
    // comfortable reading; still resizable.
    auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto area = display != nullptr ? display->userArea : juce::Rectangle<int> (0, 0, 1280, 800);
    const int w = juce::jlimit (660, 1100,
                                static_cast<int> (area.getWidth() * 0.85));
    const int h = juce::jlimit (560, 880,
                                static_cast<int> (area.getHeight() * 0.85));

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Spiral303 Manual";
    options.dialogBackgroundColour = Skins::current().background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = true;
    options.componentToCentreAround = nullptr;
    options.content.setOwned (new ManualView());
    options.content->setSize (w, h);

    dialog = options.launchAsync();
}
