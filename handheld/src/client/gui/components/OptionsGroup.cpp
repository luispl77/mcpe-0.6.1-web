#include "OptionsGroup.h"
#include "../../Minecraft.h"
#include "ImageButton.h"
#include "OptionsItem.h"
#include "Slider.h"
#include "../../../locale/I18n.h"
OptionsGroup::OptionsGroup( std::string labelID )  {
	label = I18n::get(labelID);
}

void OptionsGroup::setupPositions() {
	// First we write the header and then we add the items
	int curY = y + 10;
	for(std::vector<GuiElement*>::iterator it = children.begin(); it != children.end(); ++it) {
		(*it)->width = width - 5;

		(*it)->y = curY;
		(*it)->x = x + 10;
		(*it)->setupPositions();
		curY += (*it)->height + 3;
	}
	// A height, not a bottom edge -- OptionsPane stacks groups by adding this
	// to a running y, so leaving it absolute pushed every group after the
	// first off the bottom of the pane.
	height = curY - y;
}

void OptionsGroup::render( Minecraft* minecraft, int xm, int ym ) {
	minecraft->font->draw(label, (float)x + 2, (float)y, 0xffffffff, false);
	super::render(minecraft, xm, ym);
}

OptionsGroup& OptionsGroup::addOptionItem( const Options::Option* option, Minecraft* minecraft ) {
	if(option->isBoolean())
		createToggle(option, minecraft);
	else if(option->isProgress())
		createProgressSlider(option, minecraft);
	else if(option->isInt())
		createStepSlider(option, minecraft);
	return *this;
}

void OptionsGroup::createToggle( const Options::Option* option, Minecraft* minecraft ) {
	ImageDef def;
	def.setSrc(IntRectangle(160, 206, 39, 20));
	def.name = "gui/touchgui.png";
	def.width = 39 * 0.7f;
	def.height = 20 * 0.7f;
	OptionButton* element = new OptionButton(option);
	element->setImageDef(def, true);
	// Without this the button picks its on/off frame from an uninitialised
	// flag, so it can open showing the opposite of the actual setting.
	element->updateImage(&minecraft->options);
	std::string itemLabel = I18n::get(option->getCaptionId());
	OptionsItem* item = new OptionsItem(itemLabel, element);
	addChild(item);
	setupPositions();
}

void OptionsGroup::createProgressSlider( const Options::Option* option, Minecraft* minecraft ) {
	Slider* element = new Slider(minecraft,
									option,
									minecraft->options.getProgrssMin(option),
									minecraft->options.getProgrssMax(option));
	element->width = 100;
	element->height = 20;
	// The row is named after the option, not after the group it sits in --
	// using `label` here is what made the sensitivity row read
	// "options.group.mojang<" instead of "Sensitivity".
	OptionsItem* item = new OptionsItem(I18n::get(option->getCaptionId()), element);
	addChild(item);
	setupPositions();
}

void OptionsGroup::createStepSlider( const Options::Option* option, Minecraft* minecraft ) {
	std::vector<int> steps = minecraft->options.getSteps(option);
	if (steps.size() < 2) return;

	Slider* element = new Slider(minecraft, option, steps);
	element->width = 100;
	element->height = 20;
	OptionsItem* item = new OptionsItem(I18n::get(option->getCaptionId()), element);
	addChild(item);
	setupPositions();
}
