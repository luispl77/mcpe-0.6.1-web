#ifndef NET_MINECRAFT_CLIENT_GUI_SCREENS_TOUCH__TouchJoinGameScreen_H__
#define NET_MINECRAFT_CLIENT_GUI_SCREENS_TOUCH__TouchJoinGameScreen_H__

#include "../../Screen.h"
#include "../../components/Button.h"
#include "../../components/SmallButton.h"
#include "../../components/RolledSelectionListV.h"
#include "../../../Minecraft.h"
#include "../../../../platform/input/Multitouch.h"
#include "../../../../network/RakNetInstance.h"

namespace Touch {

class JoinGameScreen;

class AvailableGamesList : public RolledSelectionListV
{
	int startSelected;
	int selectedItem;
	ServerList copiedServerList;

	friend class JoinGameScreen;

public:
	AvailableGamesList(Minecraft* _minecraft, int _width, int _height)
	/* The list stops short of the bottom now: Join, Settings and Delete live
	 * down there, and a row scrolling underneath them would be a row you could
	 * see and not tap. */
	:	RolledSelectionListV(_minecraft, _width, _height, 0, _width, 24, _height - 28, 34),
		selectedItem(-1),
		startSelected(-1)
	{
	}

protected:

	virtual int getNumberOfItems() { return (int)copiedServerList.size(); }

	virtual void selectCancel();
	virtual void selectStart(int item);
	virtual void selectItem(int item, bool doubleClick);

	virtual bool isSelectedItem(int item) { return item == selectedItem; }

	virtual void renderBackground() {}
	virtual void renderItem(int i, int x, int y, int h, Tesselator& t);
};

class JoinGameScreen: public Screen
{
public:
	JoinGameScreen();
	virtual ~JoinGameScreen();

	void init();
	void setupPositions();

	virtual bool handleBackEvent(bool isDown);

	virtual bool isIndexValid(int index);

	virtual void tick();
	void render(int xm, int ym, float a);

	void buttonClicked(Button* button);

	bool isInGameScreen();
private:
	TButton bJoin;
	TButton bSettings;
	TButton bDelete;
	TButton bBack;
	TButton bCreate;
	THeader bHeader;
	AvailableGamesList* gamesList;

	/* Only on a deployment that has somewhere to make one. Latched in init()
	 * rather than asked every frame, because setupPositions() sizes the header
	 * around it and a button that came and went would resize the title bar. */
	bool _canCreate;

	/* Whether the selected row is a world of ours, refreshed as the selection
	 * changes rather than every frame: it is a call out to the page, and the
	 * answer only moves when the selection does. */
	bool _canManage;
	int _managedItem;
};

};

#endif /*NET_MINECRAFT_CLIENT_GUI_SCREENS_TOUCH__TouchJoinGameScreen_H__*/
