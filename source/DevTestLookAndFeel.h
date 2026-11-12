#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// DevTest colours
//==============================================================================
namespace DevTestColours
{
    inline const juce::Colour background { 0xffeae7de };   // background
    inline const juce::Colour text       { 0xff292929 };   // text
    inline const juce::Colour accent     { 0xffd52925 };   // special elements
    inline const juce::Colour traceL     { 0xffd52925 };   // channel L (all displays)
    inline const juce::Colour traceR     { 0xff292929 };   // channel R (all displays)

    // Derived tones for panels and grid lines
    inline const juce::Colour panel      = background.darker (0.06f);
    inline const juce::Colour grid       = text.withAlpha (0.12f);
    inline const juce::Colour gridStrong = text.withAlpha (0.35f);
}

//==============================================================================
// Background texture: normally distributed (Gaussian) white noise.
// Each pixel is white for positive and black for negative noise values, with an
// alpha proportional to the magnitude, so the average brightness of the
// background colour stays unchanged. The tile is drawn repeatedly with
// 'noiseOpacity' over the background.
//==============================================================================
namespace NoiseTexture
{
    inline constexpr float noiseOpacity = 0.042f;   // overall strength of the noise layer
    inline constexpr int   tileSize     = 512;     // large enough that repetition is not visible

    inline juce::Image createTile()
    {
        juce::Image tile (juce::Image::ARGB, tileSize, tileSize, true);
        juce::Random random (0x5eed);  // fixed seed -> same texture on every start

        // Box-Muller transform: two uniform random numbers -> two Gaussian values
        const auto nextGaussianPair = [&random] (float& a, float& b)
        {
            const float u1 = juce::jmax (1.0e-7f, random.nextFloat());
            const float u2 = random.nextFloat();
            const float r  = std::sqrt (-2.0f * std::log (u1));
            a = r * std::cos (juce::MathConstants<float>::twoPi * u2);
            b = r * std::sin (juce::MathConstants<float>::twoPi * u2);
        };

        juce::Image::BitmapData pixels (tile, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < tileSize; ++y)
        {
            for (int x = 0; x < tileSize; x += 2)
            {
                float n[2];
                nextGaussianPair (n[0], n[1]);

                for (int k = 0; k < 2; ++k)
                {
                    // standard deviation 1 -> alpha 0.5, clipped at 3 sigma
                    const float alpha = juce::jlimit (0.0f, 1.0f, std::abs (n[k]) * 0.5f);
                    const auto colour = (n[k] >= 0.0f ? juce::Colours::white : juce::Colours::black).withAlpha (alpha);
                    pixels.setPixelColour (x + k, y, colour);
                }
            }
        }

        return tile;
    }

    // Shared tile, created once on first use
    inline const juce::Image& getTile()
    {
        static const juce::Image tile = createTile();
        return tile;
    }

    // Fills 'area' with the noise texture at 'noiseOpacity'
    inline void draw (juce::Graphics& g, juce::Rectangle<int> area)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.setTiledImageFill (getTile(), 0, 0, noiseOpacity);
        g.fillRect (area);
    }
}

//==============================================================================
// 14 segment LED display (digits, letters and a few symbols), drawn with lit
// segments over the faint unlit ones, slightly slanted as real LED displays.
//
//   ---A---        H, J, K: upper diagonal / vertical / diagonal
//  |\  |  /|       L, M, N: lower diagonal / vertical / diagonal
//  F H J K B       G1, G2: middle left / right
//  |  \|/  |
//   -G1 G2-
//  |  /|\  |
//  E L M N C
//  |/  |  \|
//   ---D---  .
//==============================================================================
namespace LedSegments
{
    enum : uint16_t { A = 1, B = 2, C = 4, D = 8, E = 16, F = 32, G1 = 64, G2 = 128,
                      H = 256, J = 512, K = 1024, L = 2048, M = 4096, N = 8192 };

    inline uint16_t glyph (juce::juce_wchar c) noexcept
    {
        switch (c)
        {
            case '0': return A|B|C|D|E|F;          case '1': return B|C;
            case '2': return A|B|G1|G2|E|D;        case '3': return A|B|C|D|G2;
            case '4': return F|G1|G2|B|C;          case '5': return A|F|G1|G2|C|D;
            case '6': return A|F|E|D|C|G1|G2;      case '7': return A|B|C;
            case '8': return A|B|C|D|E|F|G1|G2;    case '9': return A|B|C|D|F|G1|G2;
            case 'A': return A|B|C|E|F|G1|G2;      case 'B': return A|B|C|D|J|M|G2;
            case 'C': return A|D|E|F;              case 'D': return A|B|C|D|J|M;
            case 'E': return A|D|E|F|G1;           case 'F': return A|E|F|G1;
            case 'G': return A|C|D|E|F|G2;         case 'H': return B|C|E|F|G1|G2;
            case 'I': return A|D|J|M;              case 'J': return B|C|D|E;
            case 'K': return E|F|G1|K|N;           case 'L': return D|E|F;
            case 'M': return B|C|E|F|H|K;          case 'N': return B|C|E|F|H|N;
            case 'O': return A|B|C|D|E|F;          case 'P': return A|B|E|F|G1|G2;
            case 'Q': return A|B|C|D|E|F|N;        case 'R': return A|B|E|F|G1|G2|N;
            case 'S': return A|C|D|F|G1|G2;        case 'T': return A|J|M;
            case 'U': return B|C|D|E|F;            case 'V': return E|F|L|K;
            case 'W': return B|C|E|F|L|N;          case 'X': return H|K|L|N;
            case 'Y': return H|K|M;                case 'Z': return A|D|K|L;
            case 'b': return C|D|E|F|G1|G2;        case 'c': return D|E|G1|G2;
            case 'd': return B|C|D|E|G1|G2;        case 'h': return C|E|F|G1|G2;
            case 'i': return M;                    case 'l': return E|F;
            case 'm': return C|E|G1|G2|M;          case 'n': return C|E|G1|G2;
            case 'o': return C|D|E|G1|G2;          case 'r': return E|G1;
            case 't': return D|E|F|G1;             case 'u': return C|D|E;
            case '-': return G1|G2;                case '+': return G1|G2|J|M;
            case '%': return F|C|K|L;        case '/': return K|L;
            case '\\': return H|N;                 case '(': case '<': return K|N;
            case ')': case '>': return H|L;        case '=': return G1|G2|D;
            case '*': return G1|G2|H|J|K|L|M|N;    case '_': return D;
            case '\'': return J;                   case '"': return F|J;
            case '|': return J|M;                  case 0xb0: return A|B|F|G1|G2;   // degree
            default: break;
        }
        if (c >= 'a' && c <= 'z')
            return glyph (c - 'a' + 'A');
        return 0;
    }

    // One character cell (top left x, y; height h); 'point' lights the decimal point
    inline void drawCell (juce::Graphics& g, float x, float y, float h, uint16_t mask, bool point, bool colon, juce::Colour lit)
    {
        const float w = h * 0.62f, t = h * 0.12f, gap = t * 0.14f, skew = 0.07f;
        const auto pt = [&] (float px, float py) { return juce::Point<float> (x + px + (h - py) * skew, y + py); };
        const auto bar = [&] (float x0, float y0, float x1, float y1)   // horizontal or vertical, pointed ends
        {
            juce::Path p;
            if (juce::exactlyEqual (y0, y1))
            {
                p.startNewSubPath (pt (x0 + gap, y0)); p.lineTo (pt (x0 + gap + t * 0.5f, y0 - t * 0.5f)); p.lineTo (pt (x1 - gap - t * 0.5f, y0 - t * 0.5f));
                p.lineTo (pt (x1 - gap, y0)); p.lineTo (pt (x1 - gap - t * 0.5f, y0 + t * 0.5f)); p.lineTo (pt (x0 + gap + t * 0.5f, y0 + t * 0.5f));
            }
            else
            {
                p.startNewSubPath (pt (x0, y0 + gap)); p.lineTo (pt (x0 + t * 0.5f, y0 + gap + t * 0.5f)); p.lineTo (pt (x0 + t * 0.5f, y1 - gap - t * 0.5f));
                p.lineTo (pt (x0, y1 - gap)); p.lineTo (pt (x0 - t * 0.5f, y1 - gap - t * 0.5f)); p.lineTo (pt (x0 - t * 0.5f, y0 + gap + t * 0.5f));
            }
            p.closeSubPath();
            return p;
        };
        const auto diagonal = [&] (float x0, float y0, float x1, float y1)
        {
            const auto a = pt (x0, y0), b = pt (x1, y1);
            const auto d = (b - a) * (1.0f / a.getDistanceFrom (b)) * (gap + t * 0.75f);
            juce::Path line, p;
            line.startNewSubPath (a + d);
            line.lineTo (b - d);
            juce::PathStrokeType (t * 0.65f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt).createStrokedPath (p, line);
            return p;
        };

        const float l = t * 0.5f, r = w - t * 0.5f, cx = w * 0.5f, top = t * 0.5f, mid = h * 0.5f, bottom = h - t * 0.5f;
        const juce::Path segments[14] {
            bar (l, top, r, top),            bar (r, top, r, mid),           bar (r, mid, r, bottom),         bar (l, bottom, r, bottom),
            bar (l, mid, l, bottom),         bar (l, top, l, mid),           bar (l, mid, cx, mid),           bar (cx, mid, r, mid),
            diagonal (l, top, cx, mid),      bar (cx, top, cx, mid),         diagonal (r, top, cx, mid),
            diagonal (l, bottom, cx, mid),   bar (cx, mid, cx, bottom),      diagonal (r, bottom, cx, mid) };

        for (int i = 0; i < 14; ++i)
        {
            g.setColour ((mask >> i) & 1 ? lit : lit.withMultipliedAlpha (0.06f));
            g.fillPath (segments[i]);
        }
        const float dot = t * 1.1f;
        g.setColour (point ? lit : lit.withMultipliedAlpha (0.08f));
        g.fillRect (juce::Rectangle<float> (dot, dot).withCentre (pt (w + t * 0.9f, bottom)));
        if (colon)
            for (float py : { h * 0.3f, h * 0.7f })
            {
                g.setColour (lit);
                g.fillRect (juce::Rectangle<float> (dot, dot).withCentre (pt (cx, py)));
            }
    }

    // Text in 'area' with the given justification; the height adapts to fit the width
    inline void draw (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                      juce::Justification justification, juce::Colour lit)
    {
        struct Cell { uint16_t mask; bool point, colon, space; };
        juce::Array<Cell> cells;
        for (auto c : text.trim())
        {
            if ((c == '.' || c == ',') && ! cells.isEmpty() && ! cells.getReference (cells.size() - 1).point && ! cells.getLast().space)
                cells.getReference (cells.size() - 1).point = true;
            else
                cells.add ({ glyph (c), c == '.' || c == ',', c == ':', c == ' ' });
        }

        float h = juce::jmin (area.getHeight() * 0.7f, 18.0f);
        const auto widthFor = [&cells] (float height)
        {
            float total = 0.0f;
            for (const auto& cell : cells)
                total += height * (cell.space ? 0.4f : 0.84f);
            return total;
        };
        const float available = area.getWidth() - 8.0f;
        if (widthFor (h) > available)
            h *= available / widthFor (h);

        const float total = widthFor (h);
        float x = justification.testFlags (juce::Justification::left)  ? area.getX() + 4.0f
                : justification.testFlags (juce::Justification::right) ? area.getRight() - 4.0f - total
                                                                         : area.getCentreX() - total * 0.5f;
        const float y = area.getCentreY() - h * 0.5f;
        for (const auto& cell : cells)
        {
            if (! cell.space)
                drawCell (g, x, y, h, cell.mask, cell.point, cell.colon, lit);
            x += h * (cell.space ? 0.4f : 0.84f);
        }
    }

    // Recessed black display window: shadow at the top, light lower edge
    inline void drawWindow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour face)
    {
        g.setColour (face);
        g.fillRect (r);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.8f), r.getTopLeft(),
                                                 juce::Colours::transparentBlack, r.getTopLeft().translated (0.0f, 5.0f), false));
        g.fillRect (r.withHeight (5.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.5f), r.getTopLeft(),
                                                 juce::Colours::transparentBlack, r.getTopLeft().translated (4.0f, 0.0f), false));
        g.fillRect (r.withWidth (4.0f));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawLine (r.getX(), r.getY() + 0.5f, r.getRight(), r.getY() + 0.5f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawLine (r.getX(), r.getBottom() + 0.5f, r.getRight() + 1.0f, r.getBottom() + 0.5f, 1.0f);
        g.drawLine (r.getRight() + 0.5f, r.getY(), r.getRight() + 0.5f, r.getBottom() + 1.0f, 1.0f);
    }
}

//==============================================================================
// Hardware-style look in the DevTest colours:
//  - buttons are flat (2D) but hint at depth: darker lower edge, light upper edge,
//    they "sink" when pressed; optional small LED window (lit in the accent colour)
//  - toggle buttons are small square push buttons with an LED
//  - group titles are dark header bars with the title in the accent colour
//  - horizontal sliders have a scale with tick marks and a dark knob cap with an accent pointer
//  - value boxes look like a small LED display (dark with accent digits)
//
// Buttons with the property "led" (button.getProperties().set ("led", true)) show
// an LED window that lights up when the button is on.
//==============================================================================
class DevTestLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DevTestLookAndFeel()
        : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme {
              DevTestColours::background,                 // windowBackground
              DevTestColours::panel,                      // widgetBackground
              DevTestColours::background,                 // menuBackground
              DevTestColours::text.withAlpha (0.4f),      // outline
              DevTestColours::text,                       // defaultText
              DevTestColours::accent,                     // defaultFill
              DevTestColours::background,                 // highlightedText
              DevTestColours::accent,                     // highlightedFill
              DevTestColours::text })                     // menuText
    {
        using namespace DevTestColours;

        setColour (juce::Slider::thumbColourId,               text);
        setColour (juce::Slider::trackColourId,               accent);
        setColour (juce::Slider::backgroundColourId,          text.withAlpha (0.7f));
        setColour (juce::Slider::rotarySliderFillColourId,    accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, text.withAlpha (0.5f));
        setColour (juce::Slider::textBoxTextColourId,         accent);
        setColour (juce::Slider::textBoxBackgroundColourId,   text);
        setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);

        setColour (juce::TextButton::buttonColourId,          buttonFace());
        setColour (juce::TextButton::buttonOnColourId,        buttonFace());
        setColour (juce::TextButton::textColourOffId,         text);
        setColour (juce::TextButton::textColourOnId,          text);

        setColour (juce::ToggleButton::textColourId,          text);
        setColour (juce::ToggleButton::tickColourId,          accent);
        setColour (juce::ToggleButton::tickDisabledColourId,  text.withAlpha (0.5f));

        setColour (juce::ComboBox::backgroundColourId,        buttonFace());
        setColour (juce::ComboBox::textColourId,              text);
        setColour (juce::ComboBox::outlineColourId,           text.withAlpha (0.6f));
        setColour (juce::ComboBox::arrowColourId,             text);

        setColour (juce::GroupComponent::outlineColourId,     text.withAlpha (0.5f));
        setColour (juce::GroupComponent::textColourId,        accent);

        setColour (juce::Label::textColourId,                 text);
        setColour (juce::Label::textWhenEditingColourId,      accent);
        setColour (juce::Label::backgroundWhenEditingColourId, text);
        setColour (juce::Label::outlineWhenEditingColourId,   accent);
        setColour (juce::TextEditor::textColourId,            accent);
        setColour (juce::TextEditor::backgroundColourId,      text);
        setColour (juce::TextEditor::highlightColourId,       accent.withAlpha (0.35f));
        setColour (juce::TextEditor::highlightedTextColourId, background);
        setColour (juce::TextEditor::focusedOutlineColourId,  accent);
        setColour (juce::CaretComponent::caretColourId,       accent);
    }

    // Face colour of buttons and boxes: slightly lighter than the background
    static juce::Colour buttonFace()   { return DevTestColours::background.brighter (0.06f); }

    // Height of the lower edge that hints at depth
    static constexpr float buttonDepth = 3.0f;

    // Face of a button: up = above the lower edge, down = sunk in
    static juce::Rectangle<float> buttonFaceArea (juce::Rectangle<float> bounds, bool down)
    {
        auto face = bounds.reduced (1.0f);
        return down ? face.withTrimmedTop (buttonDepth - 1.0f).withTrimmedBottom (1.0f)
                    : face.withTrimmedBottom (buttonDepth);
    }

    // Flat button with depth hint, used for text buttons and toggle buttons
    static void drawHardwareButton (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour face,
                                    bool highlighted, bool down)
    {
        using namespace DevTestColours;

        // Body (visible as the darker lower edge)
        g.setColour (face.darker (0.45f));
        g.fillRoundedRectangle (bounds, 2.5f);

        // Face
        const auto faceArea = buttonFaceArea (bounds, down);
        g.setColour (highlighted && ! down ? face.brighter (0.05f) : face);
        g.fillRoundedRectangle (faceArea, 2.0f);

        // Light upper edge
        if (! down)
        {
            g.setColour (face.brighter (0.5f).withAlpha (0.8f));
            g.drawHorizontalLine (juce::roundToInt (faceArea.getY()), faceArea.getX() + 2.0f, faceArea.getRight() - 2.0f);
        }

        // Outline
        g.setColour (text.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 2.5f, 1.0f);
    }

    // Small LED window
    static void drawLed (juce::Graphics& g, juce::Rectangle<float> area, bool on)
    {
        using namespace DevTestColours;

        if (on)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillRoundedRectangle (area.expanded (2.0f), 2.0f);   // glow
        }

        g.setColour (on ? accent : text.withAlpha (0.22f));
        g.fillRoundedRectangle (area, 1.0f);
        g.setColour (text.withAlpha (0.5f));
        g.drawRoundedRectangle (area, 1.0f, 0.6f);
    }

    //==========================================================================
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override
    {
        // Selected buttons with LED (e.g. tabs) stay pressed in
        const bool sunk = down || (button.getToggleState() && button.getProperties()["led"]);
        drawHardwareButton (g, button.getLocalBounds().toFloat(), backgroundColour, highlighted, sunk);

        if (button.getProperties()["led"])
        {
            const auto face = buttonFaceArea (button.getLocalBounds().toFloat(), sunk);
            drawLed (g, juce::Rectangle<float> (16.0f, 4.0f).withCentre ({ face.getCentreX(), face.getY() + 6.0f }),
                     button.getToggleState());
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool down) override
    {
        const bool led  = button.getProperties()["led"];
        const bool sunk = down || (button.getToggleState() && led);
        auto face = buttonFaceArea (button.getLocalBounds().toFloat(), sunk);
        if (led)
            face.removeFromTop (8.0f);   // text below the LED

        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                                : juce::TextButton::textColourOffId)
                           .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        // Units like "dBFS" / "dBu" keep their spelling (property "keepCase")
        const auto label = button.getProperties()["keepCase"] ? button.getButtonText() : button.getButtonText().toUpperCase();
        g.drawText (label, face, juce::Justification::centred);
    }

    // Toggle button: small square push button with LED, label to the right
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down) override
    {
        using namespace DevTestColours;

        const float size = 18.0f;
        const auto box = juce::Rectangle<float> (2.0f, ((float) button.getHeight() - size) * 0.5f, size, size);
        const bool on = button.getToggleState();

        drawHardwareButton (g, box, buttonFace(), highlighted, down || on);
        const auto face = buttonFaceArea (box, down || on);
        drawLed (g, juce::Rectangle<float> (9.0f, 3.5f).withCentre (face.getCentre()), on);

        g.setColour (text.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (button.getButtonText().toUpperCase(),
                    button.getLocalBounds().toFloat().withTrimmedLeft (size + 10.0f), juce::Justification::centredLeft);
    }

    //==========================================================================
    // Group: dark header bar with the title, thin frame around the content
    void drawGroupComponentOutline (juce::Graphics& g, int width, int height, const juce::String& title,
                                    const juce::Justification&, juce::GroupComponent&) override
    {
        using namespace DevTestColours;

        const float barHeight = 20.0f;
        g.setColour (text);
        g.fillRect (0.0f, 0.0f, (float) width, barHeight);

        g.setColour (accent);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (title.toUpperCase(), juce::Rectangle<float> (0.0f, 0.0f, (float) width, barHeight),
                    juce::Justification::centred);

        const float sideHeight = juce::jmax (0.0f, (float) height - barHeight);
        g.setColour (text.withAlpha (0.5f));
        g.fillRect (0.0f, barHeight, 1.0f, sideHeight);                          // left
        g.fillRect ((float) width - 1.0f, barHeight, 1.0f, sideHeight);         // right
        g.fillRect (0.0f, (float) height - 1.0f, (float) width, 1.0f);          // bottom
    }

    //==========================================================================
    // Combo box: flat face, thin outline, small triangle
    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace DevTestColours;

        const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, 2.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 2.0f, 1.0f);

        juce::Path arrow;
        const float cx = (float) width - 14.0f, cy = (float) height * 0.5f;
        arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::Font (juce::FontOptions (13.0f));
    }

    //==========================================================================
    // Horizontal slider: scale with tick marks, accent line up to the value,
    // dark round knob cap with an accent pointer
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        using namespace DevTestColours;

        if (style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const float cy    = (float) y + (float) height * 0.5f;
        const float left  = (float) x + 2.0f, right = (float) (x + width) - 2.0f;
        const float alpha = slider.isEnabled() ? 1.0f : 0.45f;

        // Tick marks (11, every tenth of the range)
        g.setColour (text.withAlpha (0.45f * alpha));
        for (int i = 0; i <= 10; ++i)
        {
            const float tx = left + (right - left) * (float) i / 10.0f;
            const float len = (i % 5 == 0) ? 5.0f : 3.0f;
            g.fillRect (tx - 0.5f, cy - 5.0f - len, 1.0f, len);
        }

        // Track and value
        g.setColour (text.withAlpha (0.7f * alpha));
        g.fillRect (left, cy - 1.0f, juce::jmax (0.0f, right - left), 2.0f);
        g.setColour (accent.withAlpha (alpha));
        g.fillRect (left, cy - 1.0f, juce::jmax (0.0f, sliderPos - left), 2.0f);   // never negative (value at minimum)

        // Knob cap
        const float r = 7.5f;
        const auto cap = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre ({ sliderPos, cy });
        g.setColour (text.darker (0.3f).withAlpha (alpha));
        g.fillEllipse (cap.translated (0.0f, 1.5f));          // depth hint
        g.setColour (text.brighter (0.25f).withAlpha (alpha));
        g.fillEllipse (cap);
        g.setColour (text.withAlpha (alpha));
        g.fillEllipse (cap.reduced (2.0f));
        g.setColour (accent.withAlpha (alpha));
        g.fillRect (sliderPos - 0.75f, cap.getY() + 1.5f, 1.5f, r - 1.0f);   // pointer
    }

    // Value box: small LED display (monospaced digits)
    static juce::Font displayFont()
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold));
    }

    juce::Font getLabelFont (juce::Label& label) override
    {
        if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
            return displayFont();   // value box of a slider

        return label.getFont();
    }

    // Labels with the dark display background (value boxes, calibration displays):
    // recessed LED window with segment characters, all other labels as usual
    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        if (label.findColour (juce::Label::backgroundColourId) != DevTestColours::text)
        {
            juce::LookAndFeel_V4::drawLabel (g, label);
            return;
        }
        const auto r = label.getLocalBounds().toFloat().withTrimmedRight (1.0f).withTrimmedBottom (1.0f);
        LedSegments::drawWindow (g, r, DevTestColours::text);
        if (! label.isBeingEdited())
            LedSegments::draw (g, r, label.getText(), label.getJustificationType(),
                               DevTestColours::accent.withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.5f));
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (displayFont());
        label->setColour (juce::Label::textColourId,       DevTestColours::accent);
        label->setColour (juce::Label::backgroundColourId, DevTestColours::text);
        label->setColour (juce::Label::outlineColourId,    juce::Colours::transparentBlack);
        return label;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DevTestLookAndFeel)
};
