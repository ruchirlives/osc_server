#pragma once

#include <JuceHeader.h>

struct LayoutMetrics
{
	int margin = 20;
	int buttonWidth = 150;
	int buttonHeight = 20;
	int spacingX = 10;
	int spacingY = 10;
	int labelHeight = 20;
	int numButtonRows = 4;
};

class RoundedTableWrapper : public juce::Component
{
public:
	RoundedTableWrapper(juce::TableListBox& tableRef) : table(tableRef)
	{
		addAndMakeVisible(table);
	}

	void resized() override
	{
		table.setBounds(getLocalBounds().reduced(1));
	}

	void paint(juce::Graphics& g) override
	{
		auto bounds = getLocalBounds().toFloat();
		auto colour = findColour(juce::ListBox::backgroundColourId);
		auto gradient = juce::ColourGradient(colour.brighter(0.25f), 0.0f, bounds.getY(),
			colour.darker(0.15f), 0.0f, bounds.getBottom(), false);
		g.setGradientFill(gradient);
		g.fillRoundedRectangle(bounds, 6.0f);

		g.setColour(juce::Colours::white.withAlpha(0.12f));
		g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 2.0f);
	}

private:
	juce::TableListBox& table;
};

class GlobalLookAndFeel : public juce::LookAndFeel_V4
{
public:
	GlobalLookAndFeel()
		: base(juce::Colours::darkslategrey.darker(0.25f)),
		  panel(base.brighter(0.1f)),
		  accent(juce::Colour::fromRGB(90, 224, 255)),
		  shadowColour(juce::Colours::black.withAlpha(0.35f))
	{
		setColour(juce::ResizableWindow::backgroundColourId, base);
		setColour(juce::TextButton::buttonColourId, panel);
		setColour(juce::TextButton::buttonOnColourId, accent);
		setColour(juce::TextButton::textColourOffId, juce::Colours::white);
		setColour(juce::TextButton::textColourOnId, juce::Colours::white);

		setColour(juce::ComboBox::backgroundColourId, base.brighter(0.1f));
		setColour(juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha(0.25f));
		setColour(juce::ComboBox::textColourId, juce::Colours::white);

		setColour(juce::Label::textColourId, juce::Colours::whitesmoke);

		setColour(juce::PopupMenu::backgroundColourId, base);
		setColour(juce::PopupMenu::textColourId, juce::Colours::white);
		setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.35f));
		setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

		setColour(juce::Slider::thumbColourId, accent);
		setColour(juce::Slider::trackColourId, panel.brighter(0.2f));
		setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::white.withAlpha(0.2f));

		setColour(juce::TextEditor::backgroundColourId, base.darker(0.5f));
		setColour(juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha(0.3f));
		setColour(juce::TextEditor::textColourId, juce::Colours::white);

		setColour(juce::ListBox::backgroundColourId, panel.darker(0.08f));
		setColour(juce::ListBox::outlineColourId, juce::Colours::white.withAlpha(0.15f));

		setDefaultSansSerifTypefaceName("Segoe UI");
	}

	void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) override
	{
		juce::ignoreUnused(width, height);
		auto outlineColour = textEditor.findColour(juce::TextEditor::outlineColourId);
		g.setColour(outlineColour);
		g.drawRoundedRectangle(textEditor.getLocalBounds().toFloat(), 6.0f, 1.5f);
	}

	void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
		bool isMouseOverButton, bool isButtonDown) override
	{
		auto bounds = button.getLocalBounds().toFloat();

		juce::DropShadow shadow(shadowColour, 4, { 2, 2 });
		shadow.drawForRectangle(g, bounds.toNearestInt());

		auto b = backgroundColour.interpolatedWith(juce::Colours::black, isButtonDown ? 0.25f : 0.0f);
		if (isMouseOverButton)
			b = b.brighter(0.05f);

		g.setColour(b);
		g.fillRoundedRectangle(bounds, 6.0f);
	}

	void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool isMouseOverButton, bool isButtonDown) override
	{
		juce::ignoreUnused(isMouseOverButton, isButtonDown);
		juce::Font font(juce::FontOptions{ 14.0f, juce::Font::bold });
		g.setFont(font);
		g.setColour(button.findColour(juce::TextButton::textColourOffId));
		g.drawFittedText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, 1);
	}

	void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted,
		bool shouldDrawButtonAsDown) override
	{
		juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
		auto bounds = button.getLocalBounds().toFloat().reduced(4);
		g.setColour(button.getToggleState() ? accent : panel);
		g.fillRoundedRectangle(bounds, 6.0f);

		g.setColour(juce::Colours::white.withAlpha(0.7f));
		g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

		juce::Font font(juce::FontOptions{ 12.0f, juce::Font::bold });
		g.setFont(font);
		g.setColour(juce::Colours::white);
		g.drawFittedText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, 1);
	}

	void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) override
	{
		juce::ignoreUnused(width, height);
		auto bg = textEditor.findColour(juce::TextEditor::backgroundColourId);
		g.setColour(bg);
		g.fillRoundedRectangle(textEditor.getLocalBounds().toFloat(), 6.0f);
	}

	void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override
	{
		juce::ignoreUnused(width, height);
		const float w = static_cast<float>(width);
		const float h = static_cast<float>(height);
		g.setColour(findColour(juce::PopupMenu::backgroundColourId));
		g.fillRoundedRectangle(0.0f, 0.0f, w, h, 6.0f);
		g.setColour(juce::Colours::white.withAlpha(0.15f));
		g.drawRoundedRectangle(0.0f, 0.0f, w, h, 6.0f, 1.0f);
	}

private:
	const juce::Colour base;
	const juce::Colour panel;
	const juce::Colour accent;
	const juce::Colour shadowColour;
};
