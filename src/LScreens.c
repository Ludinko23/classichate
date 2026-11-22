#include "Language.h"
#include "LScreens.h"
#ifndef CC_DISABLE_LAUNCHER
#include "CCPSServers.h"
#include "String.h"
#include "LWidgets.h"
#include "LWeb.h"
#include "Launcher.h"
#include "Gui.h"
#include "Drawer2D.h"
#include "ExtMath.h"
#include "Platform.h"
#include "Stream.h"
#include "Funcs.h"
#include "Resources.h"
#include "Logger.h"
#include "Window.h"
#include "Input.h"
#include "Options.h"
#include "Utils.h"
#include "LBackend.h"
#include "Http.h"
#include "Game.h"
#include "main.h"
#include "Friends.h"
#include <time.h>

#define LAYOUTS static const struct LLayout
#define IsBackButton(btn) (btn == CCKEY_ESCAPE || btn == CCPAD_SELECT || btn == CCPAD_2)

/*########################################################################################################################*
*---------------------------------------------------------Screen base-----------------------------------------------------*
*#########################################################################################################################*/
static void LScreen_NullFunc(struct LScreen* s) { }
CC_NOINLINE static int LScreen_IndexOf(struct LScreen* s, void* w) {
	int i;
	for (i = 0; i < s->numWidgets; i++) {
		if (s->widgets[i] == w) return i;
	}
	return -1;
}

static void LScreen_DoLayout(struct LScreen* s) {
	int i;
	for (i = 0; i < s->numWidgets; i++) 
	{
		LBackend_LayoutWidget(s->widgets[i]);
	}
}

static void LScreen_Tick(struct LScreen* s) {
	struct LWidget* w = s->selectedWidget;
	if (w && w->VTABLE->Tick) w->VTABLE->Tick(w);
	Friends_Tick();
}

void LScreen_SelectWidget(struct LScreen* s, int idx, struct LWidget* w, cc_bool was) {
	if (!w) return;
	w->selected       = true;
	s->selectedWidget = w;
	if (w->VTABLE->OnSelect) w->VTABLE->OnSelect(w, idx, was);
}

void LScreen_UnselectWidget(struct LScreen* s, int idx, struct LWidget* w) {
	if (!w) return;
	w->selected       = false;
	s->selectedWidget = NULL;
	if (w->VTABLE->OnUnselect) w->VTABLE->OnUnselect(w, idx);
}

static void LScreen_CycleSelected(struct LScreen* s, int dir) {
	struct LWidget* w;
	int index = 0, i, j;

	if (s->selectedWidget) {
		index = LScreen_IndexOf(s, s->selectedWidget) + dir;
	}

	for (j = 0; j < s->numWidgets; j++) {
		i = (index + j * dir) % s->numWidgets;
		if (i < 0) i += s->numWidgets;

		w = s->widgets[i];
		if (!w->autoSelectable) continue;

		LScreen_UnselectWidget(s, 0, s->selectedWidget);
		LScreen_SelectWidget(s, 0, w, false);
		return;
	}
}

static void LScreen_KeyDown(struct LScreen* s, int key, cc_bool was, struct InputDevice* device) {
	if (InputDevice_IsEnter(key, device)) {
		/* Shouldn't multi click when holding down Enter */
		if (was) return;

		if (s->selectedWidget && s->selectedWidget->OnClick) {
			s->selectedWidget->OnClick(s->selectedWidget);
		} else if (s->hoveredWidget && s->hoveredWidget->OnClick) {
			s->hoveredWidget->OnClick(s->hoveredWidget);
		} else if (s->onEnterWidget) {
			s->onEnterWidget->OnClick(s->onEnterWidget);
		}
		return;
	}
	
	/* Active widget takes input priority over default behaviour */
	if (s->selectedWidget && s->selectedWidget->VTABLE->KeyDown) {
		if (s->selectedWidget->VTABLE->KeyDown(s->selectedWidget, key, was, device)) return;
	}

	if (key == device->tabLauncher) {
		LScreen_CycleSelected(s, Input_IsShiftPressed() ? -1 : 1);
	} else if (key == device->upButton || key == device->leftButton) {
		LScreen_CycleSelected(s, -1);
	} else if (key == device->downButton || key == device->rightButton) {
		LScreen_CycleSelected(s,  1);
	} else if (IsBackButton(key) && s->onEscapeWidget) {
		s->onEscapeWidget->OnClick(s->onEscapeWidget);
	}
}

static void LScreen_MouseUp(struct LScreen* s, int idx) { }
static void LScreen_MouseWheel(struct LScreen* s, float delta) { }

static void LScreen_DrawBackground(struct LScreen* s, struct Context2D* ctx) {
	if (!s->title) {
		Launcher_DrawBackground(ctx, 0, 0, ctx->width, ctx->height);
		return;
	}
	Launcher_DrawBackgroundAll(ctx);
	LBackend_DrawTitle(ctx, s->title);
}

CC_NOINLINE void LScreen_Reset(struct LScreen* s) {
	int i;

	s->Activated   = LScreen_NullFunc;
	s->LoadState   = LScreen_NullFunc;
	s->Deactivated = LScreen_NullFunc;
	s->Layout      = LScreen_DoLayout;
	s->Tick        = LScreen_Tick;
	s->KeyDown     = LScreen_KeyDown;
	s->MouseUp     = LScreen_MouseUp;
	s->MouseWheel  = LScreen_MouseWheel;
	s->DrawBackground = LScreen_DrawBackground;
	s->ResetArea      = Launcher_DrawBackground;

	/* reset all widgets mouse state */
	for (i = 0; i < s->numWidgets; i++) 
	{ 
		s->widgets[i]->hovered  = false;
		s->widgets[i]->selected = false;
	}

	s->numWidgets     = 0;
	s->hoveredWidget  = NULL;
	s->selectedWidget = NULL;
}


void LScreen_AddWidget(void* screen, void* widget) {
	struct LScreen* s = (struct LScreen*)screen;
	struct LWidget* w = (struct LWidget*)widget;

	if (s->numWidgets >= s->maxWidgets)
		Process_Abort("Can't add anymore widgets to this LScreen");
	s->widgets[s->numWidgets++] = w;
}


/* Forward declarations for friends screens */
void FriendsHubScreen_SetActive(void);
void FriendsListScreen_SetActive(void);
void FriendManageScreen_SetActive(const cc_string* target);
void AddFriendScreen_SetActive(void);
void FriendRequestsScreen_SetActive(void);
void FriendRequestManageScreen_SetActive(const cc_string* from);
static void FriendsListScreen_Tick(struct LScreen* s);
static void FriendRequestsScreen_Tick(struct LScreen* s);

static void SwitchToChooseMode(void* w)    { ChooseModeScreen_SetActive(false); }
static void SwitchToColours(void* w)       { ColoursScreen_SetActive(); }
static void SwitchToDirectConnect(void* w) { DirectConnectScreen_SetActive(); }
static void SwitchToMain(void* w)          { MainScreen_SetActive(); }
static void SwitchToFriends(void* w)              { FriendsHubScreen_SetActive(); }
static void SwitchToFriendsListScreen(void* w)    { FriendsListScreen_SetActive(); }
static void SwitchToAddFriendScreen(void* w)      { AddFriendScreen_SetActive(); }
static void SwitchToFriendRequestsScreen(void* w) { FriendRequestsScreen_SetActive(); }

static void SwitchToSettings(void* w)      { OptionsScreen_SetActive(); }
static void SwitchToThemes(void* w)        { ThemesScreen_SetActive(); }
static void SwitchToUpdates(void* w)       { UpdatesScreen_SetActive(); }
/* CCPS screen switches */
static void SwitchToOptions(void* w)         { OptionsScreen_SetActive(); }
static void SwitchToGeneral(void* w)         { GeneralScreen_SetActive(); }
static void SwitchToCCPSServers(void* w)     { CCPSServersScreen_SetActive(); }
static void SwitchToManageServers(void* w)   { ManageServersScreen_SetActive(); }
static void SwitchToSwitchServer(void* w)    { SwitchServerScreen_SetActive(); }
static void SwitchToAddServer(void* w)       { AddEditServerScreen_SetActive(-1); }


/*########################################################################################################################*
*-------------------------------------------------------ChooseModeScreen--------------------------------------------------*
*#########################################################################################################################*/
static struct ChooseModeScreen {
	LScreen_Layout
	struct LLine seps[2];
	struct LButton btnEnhanced, btnClassicHax, btnClassic, btnBack;
	struct LLabel  lblHelp, lblEnhanced[2], lblClassicHax[2], lblClassic[2];
	cc_bool firstTime;
} ChooseModeScreen CC_BIG_VAR;

#define CHOOSEMODE_SCREEN_MAX_WIDGETS 12
static struct LWidget* chooseMode_widgets[CHOOSEMODE_SCREEN_MAX_WIDGETS];

LAYOUTS mode_seps0[] = { { ANCHOR_CENTRE, -5 }, { ANCHOR_CENTRE, -85 } };
LAYOUTS mode_seps1[] = { { ANCHOR_CENTRE, -5 }, { ANCHOR_CENTRE, -15 } };

LAYOUTS mode_btnEnhanced[]    = { { ANCHOR_CENTRE_MIN, -250 }, { ANCHOR_CENTRE, -120      } };
LAYOUTS mode_lblEnhanced0[]   = { { ANCHOR_CENTRE_MIN,  -85 }, { ANCHOR_CENTRE, -120 - 12 } };
LAYOUTS mode_lblEnhanced1[]   = { { ANCHOR_CENTRE_MIN,  -85 }, { ANCHOR_CENTRE, -120 + 12 } };
LAYOUTS mode_btnClassicHax[]  = { { ANCHOR_CENTRE_MIN, -250 }, { ANCHOR_CENTRE,  -50      } };
LAYOUTS mode_lblClassicHax0[] = { { ANCHOR_CENTRE_MIN,  -85 }, { ANCHOR_CENTRE,  -50 - 12 } };
LAYOUTS mode_lblClassicHax1[] = { { ANCHOR_CENTRE_MIN,  -85 }, { ANCHOR_CENTRE,  -50 + 12 } };
LAYOUTS mode_btnClassic[]     = { { ANCHOR_CENTRE_MIN, -250 }, { ANCHOR_CENTRE,   20      } };
LAYOUTS mode_lblClassic0[]    = { { ANCHOR_CENTRE_MIN,  -85 }, { ANCHOR_CENTRE,   20 - 12 } };
LAYOUTS mode_lblClassic1[]    = { { ANCHOR_CENTRE_MIN,  -85 }, { ANCHOR_CENTRE,   20 + 12 } };

LAYOUTS mode_lblHelp[] = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, 160 } };
LAYOUTS mode_btnBack[] = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, 170 } };


CC_NOINLINE static void ChooseMode_Click(cc_bool classic, cc_bool classicHacks) {
	Options_PauseSaving();
		Options_SetBool(OPT_CLASSIC_MODE, classic);
		if (classic) Options_SetBool(OPT_CLASSIC_HACKS, classicHacks);

		Options_SetBool(OPT_CUSTOM_BLOCKS,   !classic);
		Options_SetBool(OPT_CPE,             !classic);
		Options_SetBool(OPT_SERVER_TEXTURES, !classic);
		Options_SetBool(OPT_CLASSIC_TABLIST, classic);
		Options_SetBool(OPT_CLASSIC_OPTIONS, classic);
	Options_ResumeSaving();

	Options_SaveIfChanged();
	Launcher_LoadTheme();
	LBackend_UpdateTitleFont();
	MainScreen_SetActive();
}

static void UseModeEnhanced(void* w)   { ChooseMode_Click(false, false); }
static void UseModeClassicHax(void* w) { ChooseMode_Click(true,  true);  }
static void UseModeClassic(void* w)    { ChooseMode_Click(true,  false); }

static void ChooseModeScreen_Activated(struct LScreen* s_) {
	struct ChooseModeScreen* s = (struct ChooseModeScreen*)s_;
	LLine_Add(s,   &s->seps[0], 490, mode_seps0);
	LLine_Add(s,   &s->seps[1], 490, mode_seps1);

	LButton_Add(s, &s->btnEnhanced, 145, 35, "Enhanced",                        
				UseModeEnhanced,   mode_btnEnhanced);
	LLabel_Add(s,  &s->lblEnhanced[0], "&eEnables custom blocks, changing env", mode_lblEnhanced0);
	LLabel_Add(s,  &s->lblEnhanced[1], "&esettings, longer messages, and more", mode_lblEnhanced1);

	LButton_Add(s, &s->btnClassicHax, 145, 35, "Classic +hax",                     
				UseModeClassicHax, mode_btnClassicHax);
	LLabel_Add(s,  &s->lblClassicHax[0], "&eSame as Classic mode, except that",    mode_lblClassicHax0);
	LLabel_Add(s,  &s->lblClassicHax[1], "&ehacks (noclip/fly/speed) are enabled", mode_lblClassicHax1);

	LButton_Add(s, &s->btnClassic, 145, 35, "Classic",                        
				UseModeClassic,    mode_btnClassic);
	LLabel_Add(s,  &s->lblClassic[0], "&eOnly uses blocks and features from", mode_lblClassic0);
	LLabel_Add(s,  &s->lblClassic[1], "&ethe original minecraft classic",     mode_lblClassic1);

	if (s->firstTime) {
		LLabel_Add(s,  &s->lblHelp, "&eClick &fEnhanced &eif you're not sure which mode to choose.", mode_lblHelp);
	} else {
		LButton_Add(s, &s->btnBack, 80, 35, "Back", 
					SwitchToSettings, mode_btnBack);
	}
}

void ChooseModeScreen_SetActive(cc_bool firstTime) {
	struct ChooseModeScreen* s = &ChooseModeScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = chooseMode_widgets;
	s->maxWidgets = Array_Elems(chooseMode_widgets);
	s->firstTime  = firstTime;

	s->Activated      = ChooseModeScreen_Activated;
	s->title          = "Choose mode";
	s->onEnterWidget  = (struct LWidget*)&s->btnEnhanced;
	s->onEscapeWidget = firstTime ? NULL : (struct LWidget*)&s->btnBack;

	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*---------------------------------------------------------ColoursScreen---------------------------------------------------*
*#########################################################################################################################*/
#define COLOURS_NUM_ROWS 5 /* Background, border, etc */
#define COLOURS_NUM_COLS 3 /* R, G, B widgets */
#define COLOURS_NUM_ENTRIES (COLOURS_NUM_ROWS * COLOURS_NUM_COLS)

static struct ColoursScreen {
	LScreen_Layout
	struct LButton btnBack;
	struct LLabel lblNames[COLOURS_NUM_ROWS];
	struct LLabel lblRGB[COLOURS_NUM_COLS];
	struct LInput iptColours[COLOURS_NUM_ENTRIES];
	struct LCheckbox cbClassic;
} ColoursScreen CC_BIG_VAR;

#define COLOURSSCREEN_MAX_WIDGETS 25
static struct LWidget* colours_widgets[COLOURSSCREEN_MAX_WIDGETS];

#define IptColor_Layout(xx, yy) { { ANCHOR_CENTRE, xx }, { ANCHOR_CENTRE, yy } }
LAYOUTS clr_iptColours[COLOURS_NUM_ENTRIES][2] = {
	IptColor_Layout(30, -100), IptColor_Layout(95, -100), IptColor_Layout(160, -100),
	IptColor_Layout(30,  -60), IptColor_Layout(95,  -60), IptColor_Layout(160,  -60),
	IptColor_Layout(30,  -20), IptColor_Layout(95,  -20), IptColor_Layout(160,  -20),
	IptColor_Layout(30,   20), IptColor_Layout(95,   20), IptColor_Layout(160,   20),
	IptColor_Layout(30,   60), IptColor_Layout(95,   60), IptColor_Layout(160,   60),
};
LAYOUTS clr_lblNames0[] = { { ANCHOR_CENTRE_MAX, 10 }, { ANCHOR_CENTRE, -100 } };
LAYOUTS clr_lblNames1[] = { { ANCHOR_CENTRE_MAX, 10 }, { ANCHOR_CENTRE,  -60 } };
LAYOUTS clr_lblNames2[] = { { ANCHOR_CENTRE_MAX, 10 }, { ANCHOR_CENTRE,  -20 } };
LAYOUTS clr_lblNames3[] = { { ANCHOR_CENTRE_MAX, 10 }, { ANCHOR_CENTRE,   20 } };
LAYOUTS clr_lblNames4[] = { { ANCHOR_CENTRE_MAX, 10 }, { ANCHOR_CENTRE,   60 } };

LAYOUTS clr_lblRGB0[]   = { { ANCHOR_CENTRE,  30 }, { ANCHOR_CENTRE, -130 } };
LAYOUTS clr_lblRGB1[]   = { { ANCHOR_CENTRE,  95 }, { ANCHOR_CENTRE, -130 } };
LAYOUTS clr_lblRGB2[]   = { { ANCHOR_CENTRE, 160 }, { ANCHOR_CENTRE, -130 } };
LAYOUTS clr_cbClassic[] = { { ANCHOR_CENTRE, -16 }, { ANCHOR_CENTRE,  130 } };
LAYOUTS clr_btnBack[]   = { { ANCHOR_CENTRE,   0 }, { ANCHOR_CENTRE,  170 } };


CC_NOINLINE static void ColoursScreen_Set(struct LInput* w, cc_uint8 value) {
	cc_string tmp; char tmpBuffer[STRING_SIZE];
	String_InitArray(tmp, tmpBuffer);

	String_AppendInt(&tmp, value);
	LInput_SetText(w, &tmp);
}

CC_NOINLINE static void ColoursScreen_Update(struct ColoursScreen* s, int i, BitmapCol color) {
	ColoursScreen_Set(&s->iptColours[i + 0], BitmapCol_R(color));
	ColoursScreen_Set(&s->iptColours[i + 1], BitmapCol_G(color));
	ColoursScreen_Set(&s->iptColours[i + 2], BitmapCol_B(color));
}

CC_NOINLINE static void ColoursScreen_UpdateAll(struct ColoursScreen* s) {
	ColoursScreen_Update(s,  0, Launcher_Theme.BackgroundColor);
	ColoursScreen_Update(s,  3, Launcher_Theme.ButtonBorderColor);
	ColoursScreen_Update(s,  6, Launcher_Theme.ButtonHighlightColor);
	ColoursScreen_Update(s,  9, Launcher_Theme.ButtonForeColor);
	ColoursScreen_Update(s, 12, Launcher_Theme.ButtonForeActiveColor);
}

static void ColoursScreen_TextChanged(struct LInput* w) {
	struct ColoursScreen* s = &ColoursScreen;
	int index = LScreen_IndexOf((struct LScreen*)s, w);
	BitmapCol* color;
	cc_uint8 r, g, b;

	if (index < 3)       color = &Launcher_Theme.BackgroundColor;
	else if (index < 6)  color = &Launcher_Theme.ButtonBorderColor;
	else if (index < 9)  color = &Launcher_Theme.ButtonHighlightColor;
	else if (index < 12) color = &Launcher_Theme.ButtonForeColor;
	else                 color = &Launcher_Theme.ButtonForeActiveColor;

	/* if index of G input, changes to index of R input */
	index = (index / 3) * 3;
	if (!Convert_ParseUInt8(&s->iptColours[index + 0].text, &r)) return;
	if (!Convert_ParseUInt8(&s->iptColours[index + 1].text, &g)) return;
	if (!Convert_ParseUInt8(&s->iptColours[index + 2].text, &b)) return;

	*color = BitmapColor_RGB(r, g, b);
	Launcher_SaveTheme();
	LBackend_ThemeChanged();
}

static void ColoursScreen_AdjustSelected(struct LScreen* s, int delta) {
	struct LInput* w;
	int index, newVal;
	cc_uint8 value;

	if (!s->selectedWidget) return;
	index = LScreen_IndexOf(s, s->selectedWidget);
	if (index >= 15) return;

	w = (struct LInput*)s->selectedWidget;
	if (!Convert_ParseUInt8(&w->text, &value)) return;
	newVal = value + delta;

	Math_Clamp(newVal, 0, 255);
	ColoursScreen_Set(w, newVal);
	ColoursScreen_TextChanged(w);
}

static void ColoursScreen_KeyDown(struct LScreen* s, int key, cc_bool was, struct InputDevice* device) {
	int deltaX, deltaY;
	Input_CalcDelta(key, device, &deltaX, &deltaY);
	if (key == CCWHEEL_UP)   deltaX = +1;
	if (key == CCWHEEL_DOWN) deltaX = -1;
	
	if (deltaX || deltaY) {
		ColoursScreen_AdjustSelected(s, deltaY * 10 + deltaX);
	} else {
		LScreen_KeyDown(s, key, was, device);
	}
}

static void ColoursScreen_ToggleBG(struct LCheckbox* w) {
	Launcher_Theme.ClassicBackground = w->value;
	Launcher_SaveTheme();
	LBackend_ThemeChanged();
}

static void ColoursScreen_AddWidgets(struct ColoursScreen* s) {
	int i;
	for (i = 0; i < COLOURS_NUM_ENTRIES; i++)
	{
		s->iptColours[i].inputType   = KEYBOARD_TYPE_INTEGER;
		s->iptColours[i].TextChanged = ColoursScreen_TextChanged;
		LInput_Add(s, &s->iptColours[i], 55, NULL, clr_iptColours[i]);
	}

	LLabel_Add(s,  &s->lblNames[0], "Background",       clr_lblNames0);
	LLabel_Add(s,  &s->lblNames[1], "Button border",    clr_lblNames1);
	LLabel_Add(s,  &s->lblNames[2], "Button highlight", clr_lblNames2);
	LLabel_Add(s,  &s->lblNames[3], "Button",           clr_lblNames3);
	LLabel_Add(s,  &s->lblNames[4], "Active button",    clr_lblNames4);

	LLabel_Add(s,  &s->lblRGB[0], "Red",        clr_lblRGB0);
	LLabel_Add(s,  &s->lblRGB[1], "Green",      clr_lblRGB1);
	LLabel_Add(s,  &s->lblRGB[2], "Blue",       clr_lblRGB2);
	LButton_Add(s, &s->btnBack, 80, 35, "Back",
				SwitchToThemes, clr_btnBack);

	LCheckbox_Add(s, &s->cbClassic, "Classic style", 
					ColoursScreen_ToggleBG, clr_cbClassic);
}

static void ColoursScreen_Activated(struct LScreen* s_) {
	struct ColoursScreen* s = (struct ColoursScreen*)s_;
	ColoursScreen_AddWidgets(s);

	LCheckbox_Set(&s->cbClassic, Launcher_Theme.ClassicBackground);
	ColoursScreen_UpdateAll(s);
}

void ColoursScreen_SetActive(void) {
	struct ColoursScreen* s = &ColoursScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = colours_widgets;
	s->maxWidgets = Array_Elems(colours_widgets);

	s->Activated  = ColoursScreen_Activated;
	s->KeyDown    = ColoursScreen_KeyDown;

	s->title          = "Custom theme";
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;
	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*------------------------------------------------------DirectConnectScreen------------------------------------------------*
*#########################################################################################################################*/
static struct DirectConnectScreen {
	LScreen_Layout
	struct LButton btnConnect, btnBack;
	struct LInput iptUsername, iptAddress, iptMppass;
	struct LLabel lblStatus;
} DirectConnectScreen CC_BIG_VAR;

#define DIRECTCONNECT_SCREEN_MAXWIDGETS 6
static struct LWidget* directConnect_widgets[DIRECTCONNECT_SCREEN_MAXWIDGETS];

LAYOUTS dc_iptUsername[] = { { ANCHOR_CENTRE_MIN, -165 }, { ANCHOR_CENTRE, -120 } };
LAYOUTS dc_iptAddress[]  = { { ANCHOR_CENTRE_MIN, -165 }, { ANCHOR_CENTRE,  -75 } };
LAYOUTS dc_iptMppass[]   = { { ANCHOR_CENTRE_MIN, -165 }, { ANCHOR_CENTRE,  -30 } };

LAYOUTS dc_btnConnect[]  = { { ANCHOR_CENTRE, -110 }, { ANCHOR_CENTRE, 20 } };
LAYOUTS dc_btnBack[]     = { { ANCHOR_CENTRE,  125 }, { ANCHOR_CENTRE, 20 } };
LAYOUTS dc_lblStatus[]   = { { ANCHOR_CENTRE,    0 }, { ANCHOR_CENTRE, 70 } };


static void DirectConnectScreen_UrlFilter(cc_string* str) {
	cc_string addr, user, mppass;
	if (!DirectUrl_Claims(str, &addr, &user, &mppass)) return;
	
	LInput_SetString(&DirectConnectScreen.iptAddress,  &addr);
	LInput_SetString(&DirectConnectScreen.iptUsername, &user);
	LInput_SetString(&DirectConnectScreen.iptMppass,   &mppass);
	str->length = 0;
}

static cc_bool DirectConnectScreen_ParsePort(const cc_string* str) {
	int port;
	return Convert_ParseInt(str, &port) && port >= 0 && port <= 65535;
}

static void DirectConnectScreen_StartClient(void* w) {
	cc_string savedUser, savedPass;
	char savedUserBuffer[STRING_SIZE], savedPassBuffer[STRING_SIZE];
	static const cc_string defMppass = String_FromConst("(none)");
	const cc_string* user   = &DirectConnectScreen.iptUsername.text;
	const cc_string* addr   = &DirectConnectScreen.iptAddress.text;
	const cc_string* mppass = &DirectConnectScreen.iptMppass.text;
	struct LLabel* status   = &DirectConnectScreen.lblStatus;

	cc_string ip, port;
	cc_sockaddr addrs[SOCKET_MAX_ADDRS];
	int numAddrs;

	int index = String_LastIndexOf(addr, ':');
	if (index == 0 || index == addr->length - 1) {
		LLabel_SetConst(status, "&cInvalid address"); return;
	}

	if (!user->length) {
		String_InitArray(savedUser, savedUserBuffer);
		Options_Get(LOPT_USERNAME, &savedUser, "");
		user = &savedUser;
	}
	if (!user->length) {
		LLabel_SetConst(status, "&cUsername required"); return;
	}
	DirectUrl_ExtractAddress(addr, &ip, &port);
	if (!DirectConnectScreen_ParsePort(&port)) {
		LLabel_SetConst(status, "&cInvalid port"); return;
	}
	if (Socket_ParseAddress(&ip, 25565, addrs, &numAddrs)) {
		LLabel_SetConst(status, "&cInvalid ip"); return;
	}
	if (!mppass->length) {
		String_InitArray(savedPass, savedPassBuffer);
		Options_GetSecure(LOPT_PASSWORD, &savedPass);
		if (savedPass.length) mppass = &savedPass;
		else mppass = &defMppass;
	}

	Options_PauseSaving();
		Options_Set("launcher-dc-username", user);
		Options_Set("launcher-dc-ip",       &ip);
		Options_Set("launcher-dc-port",     &port);
		Options_SetSecure("launcher-dc-mppass", mppass);
	Options_ResumeSaving();

	LLabel_SetConst(status, "");
	Launcher_StartGame(user, mppass, &ip, &port, &String_Empty, 1);
}

static void DirectConnectScreen_Activated(struct LScreen* s_) {
	struct DirectConnectScreen* s = (struct DirectConnectScreen*)s_;

	LInput_Add(s,  &s->iptUsername, 330, "Username..",               dc_iptUsername);
	LInput_Add(s,  &s->iptAddress,  330, "IP address:Port number or ccjoin://..", dc_iptAddress);
	LInput_Add(s,  &s->iptMppass,   330, "Mppass..",                 dc_iptMppass);

	LButton_Add(s, &s->btnConnect, 110, 35, "Connect", 
				DirectConnectScreen_StartClient, dc_btnConnect);
	LButton_Add(s, &s->btnBack,     80, 35, "Back",    
				SwitchToMain, dc_btnBack);
	LLabel_Add(s,  &s->lblStatus,  "", dc_lblStatus);

	s->iptUsername.ClipboardFilter = DirectConnectScreen_UrlFilter;
	s->iptAddress.ClipboardFilter  = DirectConnectScreen_UrlFilter;
	s->iptMppass.ClipboardFilter   = DirectConnectScreen_UrlFilter;
}

static void DirectConnectScreen_Load(struct LScreen* s_) {
	struct DirectConnectScreen* s = (struct DirectConnectScreen*)s_;
	cc_string addr; char addrBuffer[STRING_SIZE];
	cc_string mppass; char mppassBuffer[STRING_SIZE];
	cc_string user, savedUser, ip, port;
	char savedUserBuffer[STRING_SIZE];
	Options_Reload();

	Options_UNSAFE_Get("launcher-dc-username", &user);
	Options_UNSAFE_Get("launcher-dc-ip",       &ip);
	Options_UNSAFE_Get("launcher-dc-port",     &port);
	
	String_InitArray(mppass, mppassBuffer);
	Options_GetSecure("launcher-dc-mppass", &mppass);
	if (!mppass.length) {
		Options_GetSecure(LOPT_PASSWORD, &mppass);
	}

	if (!user.length) {
		String_InitArray(savedUser, savedUserBuffer);
		Options_Get(LOPT_USERNAME, &savedUser, "");
		user = savedUser;
	}

	String_InitArray(addr, addrBuffer);
	String_Format2(&addr, "%s:%s", &ip, &port);

	/* don't want just ':' for address */
	if (addr.length == 1) addr.length = 0;
	
	LInput_SetText(&s->iptUsername, &user);
	LInput_SetText(&s->iptAddress,  &addr);
	LInput_SetText(&s->iptMppass,   &mppass);
}

void DirectConnectScreen_SetActive(void) {
	struct DirectConnectScreen* s = &DirectConnectScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = directConnect_widgets;
	s->maxWidgets = Array_Elems(directConnect_widgets);

	s->Activated      = DirectConnectScreen_Activated;
	s->LoadState      = DirectConnectScreen_Load;
	s->title          = "Direct connect";
	s->onEnterWidget  = (struct LWidget*)&s->btnConnect;
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;

	Launcher_SetScreen((struct LScreen*)s);
}

void DirectConnectScreen_SetActiveWithValues(const cc_string* username, const cc_string* server) {
	struct DirectConnectScreen* s = &DirectConnectScreen;
	cc_string empty;
	char emptyBuffer[1] = { 0 };
	LScreen_Reset((struct LScreen*)s);
	DirectConnectScreen_SetActive();
	String_InitArray_NT(empty, emptyBuffer);
	if (username) LInput_SetText(&s->iptUsername, username);
	if (server)   LInput_SetText(&s->iptAddress,  server);
	LInput_SetText(&s->iptMppass, &empty);
}


/*########################################################################################################################*
*----------------------------------------------------------MFAScreen------------------------------------------------------*
*#########################################################################################################################*/
static struct MFAScreen {
	LScreen_Layout
	struct LInput iptCode;
	struct LButton btnSignIn, btnCancel;
	struct LLabel  lblTitle;
} MFAScreen CC_BIG_VAR;

#define MFA_SCREEN_MAX_WIDGETS 4
static struct LWidget* mfa_widgets[MFA_SCREEN_MAX_WIDGETS];

LAYOUTS mfa_lblTitle[]  = { { ANCHOR_CENTRE,   0 }, { ANCHOR_CENTRE, -115 } };
LAYOUTS mfa_iptCode[]   = { { ANCHOR_CENTRE,   0 }, { ANCHOR_CENTRE,  -75 } };
LAYOUTS mfa_btnSignIn[] = { { ANCHOR_CENTRE, -90 }, { ANCHOR_CENTRE,  -25 } };
LAYOUTS mfa_btnCancel[] = { { ANCHOR_CENTRE,  90 }, { ANCHOR_CENTRE,  -25 } };


static void MainScreen_DoLogin(void);
static void MFAScreen_SignIn(void* w) {
	MainScreen_SetActive();
	MainScreen_DoLogin();
}
static void MFAScreen_Cancel(void* w) {
	LInput_ClearText(&MFAScreen.iptCode);
	MainScreen_SetActive();
}

static void MFAScreen_Activated(struct LScreen* s_) {
	struct MFAScreen* s = (struct MFAScreen*)s_;
	s->iptCode.inputType = KEYBOARD_TYPE_INTEGER;
	
	LLabel_Add(s,  &s->lblTitle,  "",                  mfa_lblTitle);
	LInput_Add(s,  &s->iptCode,   280, "Login code..", mfa_iptCode);
	LButton_Add(s, &s->btnSignIn, 100, 35, "Sign in",  
				MFAScreen_SignIn, mfa_btnSignIn);
	LButton_Add(s, &s->btnCancel, 100, 35, "Cancel",   
				MFAScreen_Cancel, mfa_btnCancel);

	LLabel_SetConst(&s->lblTitle, s->iptCode.text.length ?
		"&cWrong code entered  (Check emails)" :
		"&cLogin code required (Check emails)");
}

void MFAScreen_SetActive(void) {
	struct MFAScreen* s = &MFAScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = mfa_widgets;
	s->maxWidgets = Array_Elems(mfa_widgets);

	s->Activated     = MFAScreen_Activated;
	s->title         = "Enter login code";
	s->onEnterWidget = (struct LWidget*)&s->btnSignIn;

	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*----------------------------------------------------------SplitScreen----------------------------------------------------*
*#########################################################################################################################*/
#ifdef CC_BUILD_SPLITSCREEN
static struct SplitScreen {
	LScreen_Layout
	struct LButton btnPlayers[3], btnBack;
	cc_bool signingIn;
} SplitScreen CC_BIG_VAR;

#define SPLITSCREEN_MAX_WIDGETS 4
static struct LWidget* split_widgets[SPLITSCREEN_MAX_WIDGETS];

LAYOUTS sps_btnPlayers2[] = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, -120 } };
LAYOUTS sps_btnPlayers3[] = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  -70 } };
LAYOUTS sps_btnPlayers4[] = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  -20 } };
LAYOUTS sps_btnBack[]     = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  170 } };

static void SplitScreen_Start(int players) {
	static const cc_string user = String_FromConst(DEFAULT_USERNAME);
	Launcher_StartGame(&user, &String_Empty, &String_Empty, &String_Empty, &String_Empty, players);
}

static void SplitScreen_Players2(void* w) { SplitScreen_Start(2); }
static void SplitScreen_Players3(void* w) { SplitScreen_Start(3); }
static void SplitScreen_Players4(void* w) { SplitScreen_Start(4); }

static void SplitScreen_Activated(struct LScreen* s_) {
	struct SplitScreen* s = (struct SplitScreen*)s_;

	LButton_Add(s, &s->btnPlayers[0], 300, 35, "2 player splitscreen", 
				SplitScreen_Players2, sps_btnPlayers2);
	LButton_Add(s, &s->btnPlayers[1], 300, 35, "3 player splitscreen", 
				SplitScreen_Players3, sps_btnPlayers3);
	LButton_Add(s, &s->btnPlayers[2], 300, 35, "4 player splitscreen", 
				SplitScreen_Players4, sps_btnPlayers4);

	LButton_Add(s, &s->btnBack, 100, 35, "Back", 
				SwitchToMain, sps_btnBack);
}

void SplitScreen_SetActive(void) {
	struct SplitScreen* s = &SplitScreen;
	LScreen_Reset((struct LScreen*)s);
	
	s->widgets    = split_widgets;
	s->maxWidgets = Array_Elems(split_widgets);

	s->Activated     = SplitScreen_Activated;
	s->title         = "Splitscreen mode";

	Launcher_SetScreen((struct LScreen*)s);
}

static void SwitchToSplitScreen(void* w) { SplitScreen_SetActive(); }
#endif


/*########################################################################################################################*
*----------------------------------------------------------MainScreen-----------------------------------------------------*
*#########################################################################################################################*/
static struct MainScreen {
	LScreen_Layout
	struct LButton btnLogin, btnResume, btnDirect, btnSPlayer, btnSplit;
	struct LButton btnRegister, btnOptions, btnUpdates;
	struct LInput iptUsername, iptPassword;
	struct LLabel lblStatus, lblUpdate;
	cc_bool signingIn;
} MainScreen CC_BIG_VAR;

#define MAINSCREEN_MAX_WIDGETS 12
static struct LWidget* main_widgets[MAINSCREEN_MAX_WIDGETS];

LAYOUTS main_iptUsername[] = { { ANCHOR_CENTRE_MIN, -140 }, { ANCHOR_CENTRE, -120 } };
LAYOUTS main_iptPassword[] = { { ANCHOR_CENTRE_MIN, -140 }, { ANCHOR_CENTRE,  -75 } };

LAYOUTS main_btnLogin[]  = { { ANCHOR_CENTRE, -90 }, { ANCHOR_CENTRE, -25 } };
LAYOUTS main_lblStatus[] = { { ANCHOR_CENTRE,   0 }, { ANCHOR_CENTRE,  20 } };

LAYOUTS main_btnResume[]  = { { ANCHOR_CENTRE, 90 }, { ANCHOR_CENTRE, -25 } };
LAYOUTS main_btnDirect[]  = { { ANCHOR_CENTRE,  0 }, { ANCHOR_CENTRE,  60 } };
LAYOUTS main_btnSPlayer[] = { { ANCHOR_CENTRE,  0 }, { ANCHOR_CENTRE, 110 } };
LAYOUTS main_btnSplit[]   = { { ANCHOR_CENTRE,  0 }, { ANCHOR_CENTRE, 160 } };

LAYOUTS main_btnRegister[] = { { ANCHOR_MIN,    6 }, { ANCHOR_MAX,  6 } };
LAYOUTS main_btnOptions[]  = { { ANCHOR_CENTRE, 0 }, { ANCHOR_MAX,  6 } };
LAYOUTS main_btnUpdates[]  = { { ANCHOR_MAX,    6 }, { ANCHOR_MAX,  6 } };

LAYOUTS main_lblUpdate_N[] = { { ANCHOR_MAX,   10 }, { ANCHOR_MAX, 45 } };
LAYOUTS main_lblUpdate_H[] = { { ANCHOR_MAX,   10 }, { ANCHOR_MAX, 6 } };

CC_NOINLINE static void MainScreen_Error(struct HttpRequest* req, const char* action) {
	cc_string str; char strBuffer[STRING_SIZE * 4];
	struct MainScreen* s = &MainScreen;
	String_InitArray(str, strBuffer);

	Launcher_DisplayHttpError(req, action, &str);
	LLabel_SetText(&s->lblStatus, &str);
	s->signingIn = false;
}

static void MainScreen_DoLogin(void) {
	struct MainScreen* s = &MainScreen;
	cc_string* user = &s->iptUsername.text;
	cc_string* pass = &s->iptPassword.text;

	if (!user->length) {
		LLabel_SetConst(&s->lblStatus, "&cUsername required"); return;
	}
	if (!pass->length) {
		LLabel_SetConst(&s->lblStatus, "&cPassword required"); return;
	}

	if (GetTokenTask.Base.working) return;
	Options_Set(LOPT_USERNAME, user);
	Options_SetSecure(LOPT_PASSWORD, pass);

	GetTokenTask_Run();
	LLabel_SetConst(&s->lblStatus, "&eSigning in..");
	s->signingIn = true;
}
static void MainScreen_Login(void* w) { MainScreen_DoLogin(); }

static void MainScreen_Register(void* w) {
	static const cc_string regUrl = String_FromConst(REGISTERNEW_URL);
	cc_result res = Process_StartOpen(&regUrl);
	if (res) Logger_SimpleWarn(res, "opening register webpage in browser");
}

static void MainScreen_Resume(void* w) {
	struct ResumeInfo info;

	if (!Resume_Parse(&info, true)) return;
	Launcher_StartGame(&info.user, &info.mppass, &info.ip, &info.port, &info.server, 1);
}

static void MainScreen_Singleplayer(void* w) {
	static const cc_string defUser = String_FromConst(DEFAULT_USERNAME);
	const cc_string* user = &MainScreen.iptUsername.text;

	if (!user->length) user = &defUser;
	Launcher_StartGame(user, &String_Empty, &String_Empty, &String_Empty, &String_Empty, 1);
}

static void MainScreen_ResumeHover(void* w) {
	cc_string str; char strBuffer[STRING_SIZE * 4];
	struct MainScreen* s = &MainScreen;
	struct ResumeInfo info;
	if (s->signingIn) return;

	Resume_Parse(&info, false);
	if (!info.user.length) return;
	String_InitArray(str, strBuffer);

	if (info.server.length && String_Equals(&info.user,	&s->iptUsername.text)) {
		String_Format1(&str, Lang_Get("&eResume to %s"), &info.server);
	} else if (info.server.length) {
		String_Format2(&str, Lang_Get("&eResume as %s to %s"), &info.user, &info.server);
	} else {
		String_Format3(&str, Lang_Get("&eResume as %s to %s:%s"), &info.user, &info.ip, &info.port);
	}
	LLabel_SetText(&s->lblStatus, &str);
}

static void MainScreen_ResumeUnhover(void* w) {
	struct MainScreen* s = &MainScreen;
	if (s->signingIn) return;

	LLabel_SetConst(&s->lblStatus, "");
}

CC_NOINLINE static cc_uint32 MainScreen_GetVersion(const cc_string* version) {
	cc_uint8 raw[4] = { 0, 0, 0, 0 };
	cc_string parts[4];
	int i, count;
	
	/* 1.0.1 -> { 1, 0, 1, 0 } */
	count = String_UNSAFE_Split(version, '.', parts, 4);
	for (i = 0; i < count; i++) 
	{
		Convert_ParseUInt8(&parts[i], &raw[i]);
	}
	return Stream_GetU32_BE(raw);
}

static void MainScreen_ApplyUpdateLabel(struct MainScreen* s) {
	static const cc_string currentStr = String_FromConst(GAME_APP_VER);
	cc_uint32 latest, current;

	if (CheckUpdateTask.Base.success) {
		latest  = MainScreen_GetVersion(&CheckUpdateTask.latestRelease);
		current = MainScreen_GetVersion(&currentStr);
#ifdef CC_BUILD_FLATPAK
		LLabel_SetConst(&s->lblUpdate, latest > current ? "&aUpdate available" : "&eUp to date");
#else
		LLabel_SetConst(&s->lblUpdate, latest > current ? "&aNew release" : "&eUp to date");
#endif
	} else {
		LLabel_SetConst(&s->lblUpdate, "&cCheck failed");
	}
}

static void MainScreen_ExitApp(void* w) {
	Window_Main.Exists = false;
}

static void MainScreen_Activated(struct LScreen* s_) {
	struct MainScreen* s = (struct MainScreen*)s_;

	s->iptPassword.inputType = KEYBOARD_TYPE_PASSWORD;
	s->lblUpdate.small       = true;

#ifdef CC_BUILD_NETWORKING
	LInput_Add(s,  &s->iptUsername, 280, "Username..",  main_iptUsername);
	LInput_Add(s,  &s->iptPassword, 280, "Password..",  main_iptPassword);
	LButton_Add(s, &s->btnLogin,    100, 35, "Sign in", 
				MainScreen_Login,  main_btnLogin);
	LButton_Add(s, &s->btnResume,   100, 35, "Resume",  
				MainScreen_Resume, main_btnResume);
#endif

	LLabel_Add(s,  &s->lblStatus,  "",  main_lblStatus);
#ifdef CC_BUILD_NETWORKING
	LButton_Add(s, &s->btnDirect,  200, 35, "Direct connect",
				SwitchToDirectConnect,   main_btnDirect);
#endif
	LButton_Add(s, &s->btnSPlayer, 200, 35, "Singleplayer",
				MainScreen_Singleplayer, main_btnSPlayer);
#ifdef CC_BUILD_SPLITSCREEN
	LButton_Add(s, &s->btnSplit,   200, 35, "Splitscreen (WIP)", 
				SwitchToSplitScreen,     main_btnSplit);
#endif

	if (Process_OpenSupported) {
		LButton_Add(s, &s->btnRegister, 100, 35, "Register", 
					MainScreen_Register, main_btnRegister);
	}

	LButton_Add(s, &s->btnOptions, 100, 35, "Options", 
				SwitchToSettings, main_btnOptions);

	if (Platform_Flags & PLAT_FLAG_APP_EXIT) {
		LLabel_Add(s,  &s->lblUpdate,  "&eChecking..", main_lblUpdate_N);
		LButton_Add(s, &s->btnUpdates,  100, 35, "Exit", 
					MainScreen_ExitApp, main_btnUpdates);
	} else {
		LLabel_Add(s,  &s->lblUpdate,  "&eChecking..",      
					Updater_Supported ? main_lblUpdate_N : main_lblUpdate_H);
		if (Updater_Supported) {
			LButton_Add(s, &s->btnUpdates,  100, 35, "Updates", 
						SwitchToUpdates, main_btnUpdates);
		}
	}

#ifdef CC_BUILD_NETWORKING
	s->btnResume.OnHover   = MainScreen_ResumeHover;
	s->btnResume.OnUnhover = MainScreen_ResumeUnhover;

	if (CheckUpdateTask.Base.completed)
		MainScreen_ApplyUpdateLabel(s);
#endif
}

static void MainScreen_Load(struct LScreen* s_) {
	cc_string user, pass; char passBuffer[STRING_SIZE];
	struct MainScreen* s = (struct MainScreen*)s_;

	String_InitArray(pass, passBuffer);
	Options_UNSAFE_Get(LOPT_USERNAME, &user);
	Options_GetSecure(LOPT_PASSWORD, &pass);

	LInput_SetText(&s->iptUsername, &user);
	LInput_SetText(&s->iptPassword, &pass);

	/* Auto sign in when automatically joining a server */
	if (!Launcher_AutoHash.length)    return;
	if (!user.length || !pass.length) return;
	MainScreen_DoLogin();
}

static void MainScreen_TickCheckUpdates(struct MainScreen* s) {
	if (!CheckUpdateTask.Base.working)   return;
	LWebTask_Tick(&CheckUpdateTask.Base, NULL);

	if (!CheckUpdateTask.Base.completed) return;
	MainScreen_ApplyUpdateLabel(s);
}

static void MainScreen_LoginPhase2(struct MainScreen* s, const cc_string* user) {
	/* website returns case correct username */
	if (!String_Equals(&s->iptUsername.text, user)) {
		LInput_SetText(&s->iptUsername, user);
	}
	String_Copy(&Launcher_Username, user);

	FetchServersTask_Run();
	LLabel_SetConst(&s->lblStatus, "&eRetrieving servers list..");
}

static void MainScreen_SignInError(struct HttpRequest* req) {
	MainScreen_Error(req, "signing in");
}

static void MainScreen_TickGetToken(struct MainScreen* s) {
	if (!GetTokenTask.Base.working)   return;
	LWebTask_Tick(&GetTokenTask.Base, MainScreen_SignInError);

	if (!GetTokenTask.Base.completed) return;
	if (!GetTokenTask.Base.success)   return;

	if (!GetTokenTask.error && String_CaselessEquals(&GetTokenTask.username, &s->iptUsername.text)) {
		/* Already logged in, go straight to fetching servers */
		MainScreen_LoginPhase2(s, &GetTokenTask.username);
	} else {
		SignInTask_Run(&s->iptUsername.text, &s->iptPassword.text,
						&MFAScreen.iptCode.text);
	}
}

static void MainScreen_TickSignIn(struct MainScreen* s) {
	if (!SignInTask.Base.working)   return;
	LWebTask_Tick(&SignInTask.Base, MainScreen_SignInError);
	if (!SignInTask.Base.completed) return;

	if (SignInTask.needMFA) { MFAScreen_SetActive(); return; }

	if (SignInTask.error) {
		LLabel_SetConst(&s->lblStatus, SignInTask.error);
	} else if (SignInTask.Base.success) {
		MainScreen_LoginPhase2(s, &SignInTask.username);
	}
}

static void MainScreen_ServersError(struct HttpRequest* req) {
	/* Show a friendlier message when a custom private server (PS) is active */
	if (CCPS_GetActiveBaseURL()) {
		LLabel_SetConst(&MainScreen.lblStatus, "&cCan't Connect To The PS!");
		MainScreen.signingIn = false;
	} else {
		MainScreen_Error(req, "retrieving servers list");
	}
}

static void MainScreen_TickFetchServers(struct MainScreen* s) {
	if (!FetchServersTask.Base.working)   return;
	LWebTask_Tick(&FetchServersTask.Base, MainScreen_ServersError);

	if (!FetchServersTask.Base.completed) return;
	if (!FetchServersTask.Base.success)   return;

	s->signingIn = false;
	if (Launcher_AutoHash.length) {
		Launcher_ConnectToServer(&Launcher_AutoHash);
		Launcher_AutoHash.length = 0;
	} else {
		ServersScreen_SetActive();
	}
}

static void MainScreen_UpdateTitle(void);

static void MainScreen_Tick(struct LScreen* s_) {
	struct MainScreen* s = (struct MainScreen*)s_;
	LScreen_Tick(s_);

	MainScreen_UpdateTitle();

#ifdef CC_BUILD_NETWORKING
	MainScreen_TickCheckUpdates(s);
	MainScreen_TickGetToken(s);
	MainScreen_TickSignIn(s);
	MainScreen_TickFetchServers(s);
#endif
}

static void MainScreen_UpdateTitle(void) {
	time_t now;
	struct tm* date;
	int mon, day;

	MainScreen.title = "ClassicHATE";

	now  = time(NULL);
	date = localtime(&now);
	if (!date) return;

	mon = date->tm_mon;   /* 0 = January */
	day = date->tm_mday;

	if (mon == 0 && day == 1) {
		MainScreen.title = "New Year, New HATE!";
        } else if (mon == 11 && day == 31) {
                MainScreen.title = "Almost New Years!";
	} else if (mon == 1 && day == 14) {
		MainScreen.title = "ClassicLOVE";
	} else if (mon == 3 && day == 1) {
		MainScreen.title = "ClassicLOVE";
	} else if (mon == 9 && day == 31) {
		MainScreen.title = "SpookyHATE";
	} else if (mon == 11 && day == 24) {
		MainScreen.title = "Merry HATEmas!";
	} else if (mon == 11 && (day == 25 || day == 26)) {
		MainScreen.title = "ChristmasHATE";
	}
}

void MainScreen_SetActive(void) {
	struct MainScreen* s = &MainScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = main_widgets;
	s->maxWidgets = Array_Elems(main_widgets);

	s->Activated = MainScreen_Activated;
	s->LoadState = MainScreen_Load;
	s->Tick      = MainScreen_Tick;

	MainScreen_UpdateTitle();                 /* set the title first */
	Launcher_SetScreen((struct LScreen*)s);   /* then actually show the screen */
}
#ifdef CC_BUILD_RESOURCES
/*########################################################################################################################*
*----------------------------------------------------CheckResourcesScreen-------------------------------------------------*
*#########################################################################################################################*/
static struct CheckResourcesScreen {
	LScreen_Layout
	struct LLabel  lblLine1, lblLine2, lblStatus;
	struct LButton btnYes, btnNo;
} CheckResourcesScreen CC_BIG_VAR;

#define CHECKRESOURCES_SCREEN_MAX_WIDGET 5
static struct LWidget* checkResources_widgets[CHECKRESOURCES_SCREEN_MAX_WIDGET];

LAYOUTS cres_lblLine1[]  = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, -50 } };
LAYOUTS cres_lblLine2[]  = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, -30 } };
LAYOUTS cres_lblStatus[] = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  10 } };

LAYOUTS cres_btnYes[] = { { ANCHOR_CENTRE, -70 }, { ANCHOR_CENTRE, 45 } };
LAYOUTS cres_btnNo[]  = { { ANCHOR_CENTRE,  70 }, { ANCHOR_CENTRE, 45 } };


static void CheckResourcesScreen_Yes(void*  w) { FetchResourcesScreen_SetActive(); }
static void CheckResourcesScreen_Next(void* w) {
	Http_ClearPending();
	if (Options_LoadResult != ReturnCode_FileNotFound) {
		MainScreen_SetActive();
	} else {
		ChooseModeScreen_SetActive(true);
	}
}

static void CheckResourcesScreen_AddWidgets(struct CheckResourcesScreen* s) {
	const char* line1_msg = Resources_MissingRequired ? "Some required resources weren't found" 
														: "Some optional resources weren't found";
	s->lblStatus.small = true;

	LLabel_Add(s,  &s->lblLine1,  line1_msg,           cres_lblLine1);
	LLabel_Add(s,  &s->lblLine2,  "Okay to download?", cres_lblLine2);
	LLabel_Add(s,  &s->lblStatus, "",                  cres_lblStatus);

	LButton_Add(s, &s->btnYes, 70, 35, "Yes", 
				CheckResourcesScreen_Yes,  cres_btnYes);
	LButton_Add(s, &s->btnNo,  70, 35, "No",  
				CheckResourcesScreen_Next, cres_btnNo);
}

static void CheckResourcesScreen_Activated(struct LScreen* s_) {
	struct CheckResourcesScreen* s = (struct CheckResourcesScreen*)s_;
	cc_string str; char buffer[STRING_SIZE * 4];
	float size;
	CheckResourcesScreen_AddWidgets(s);
		
	size = Resources_MissingSize / 1024.0f;
	String_InitArray(str, buffer);
	String_Format1(&str, Lang_Get("&eDownload size: %f2 megabytes"), &size);
	LLabel_SetText(&s->lblStatus, &str);
}

static void CheckResourcesScreen_ResetArea(struct Context2D* ctx, int x, int y, int width, int height) {
	int R = BitmapCol_R(Launcher_Theme.BackgroundColor) * 0.78f; /* 153 -> 120 */
	int G = BitmapCol_G(Launcher_Theme.BackgroundColor) * 0.70f; /* 127 ->  89 */
	int B = BitmapCol_B(Launcher_Theme.BackgroundColor) * 0.88f; /* 172 -> 151 */

	Gradient_Noise(ctx, BitmapColor_RGB(R, G, B), 4, x, y, width, height);
}

static void CheckResourcesScreen_DrawBackground(struct LScreen* s, struct Context2D* ctx) {
	int x, y, width, height;
	BitmapCol color = BitmapColor_Scale(Launcher_Theme.BackgroundColor, 0.2f);
	Context2D_Clear(ctx, color, 0, 0, ctx->width, ctx->height);
	width  = Display_ScaleX(380);
	height = Display_ScaleY(140);

	x = Gui_CalcPos(ANCHOR_CENTRE, 0, width,  ctx->width);
	y = Gui_CalcPos(ANCHOR_CENTRE, 0, height, ctx->height);
	CheckResourcesScreen_ResetArea(ctx, x, y, width, height);
}

void CheckResourcesScreen_SetActive(void) {
	struct CheckResourcesScreen* s = &CheckResourcesScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = checkResources_widgets;
	s->maxWidgets = Array_Elems(checkResources_widgets);

	s->Activated      = CheckResourcesScreen_Activated;
	s->DrawBackground = CheckResourcesScreen_DrawBackground;
	s->ResetArea      = CheckResourcesScreen_ResetArea;
	s->onEnterWidget  = (struct LWidget*)&s->btnYes;

	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*----------------------------------------------------FetchResourcesScreen-------------------------------------------------*
*#########################################################################################################################*/
static struct FetchResourcesScreen {
	LScreen_Layout
	struct LLabel  lblStatus;
	struct LButton btnCancel;
	struct LSlider sdrProgress;
} FetchResourcesScreen CC_BIG_VAR;

#define FETCHRESOURCES_SCREEN_MAX_WIDGETS 3
static struct LWidget* fetchResources_widgets[FETCHRESOURCES_SCREEN_MAX_WIDGETS];

LAYOUTS fres_lblStatus[]   = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, -10 } };
LAYOUTS fres_btnCancel[]   = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  45 } };
LAYOUTS fres_sdrProgress[] = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  15 } };


static void FetchResourcesScreen_Error(struct HttpRequest* req) {
	cc_string str; char buffer[STRING_SIZE * 4];
	String_InitArray(str, buffer);

	Launcher_DisplayHttpError(req, "downloading resources", &str);
	LLabel_SetText(&FetchResourcesScreen.lblStatus, &str);
}

static void FetchResourcesScreen_Activated(struct LScreen* s_) {
	struct FetchResourcesScreen* s = (struct FetchResourcesScreen*)s_;
	s->lblStatus.small = true;

	LLabel_Add(s,  &s->lblStatus,   "",    fres_lblStatus);
	LButton_Add(s, &s->btnCancel,   120, 35, "Cancel",                   
				CheckResourcesScreen_Next, fres_btnCancel);
	LSlider_Add(s, &s->sdrProgress, 200, 12, BitmapColor_RGB(0, 220, 0), fres_sdrProgress);

	Fetcher_ErrorCallback = FetchResourcesScreen_Error;
	Fetcher_Run(); 
}

static void FetchResourcesScreen_UpdateStatus(struct FetchResourcesScreen* s, int reqID) {
	cc_string str; char strBuffer[STRING_SIZE * 4];
	const char* name;
	int count;

	name = Fetcher_RequestName(reqID);
	if (!name) return;

	String_InitArray(str, strBuffer);
	count = Fetcher_Downloaded + 1;
	String_Format3(&str, Lang_Get("&eFetching %c.. (%i/%i)"), name, &count, &Resources_MissingCount);

	if (String_Equals(&str, &s->lblStatus.text)) return;
	LLabel_SetText(&s->lblStatus, &str);
}

static void FetchResourcesScreen_UpdateProgress(struct FetchResourcesScreen* s) {
	int reqID, progress;

	if (!Http_GetCurrent(&reqID, &progress)) return;
	FetchResourcesScreen_UpdateStatus(s, reqID);
	/* making request still, haven't started download yet */
	if (progress < 0 || progress > 100) return;

	LSlider_SetProgress(&s->sdrProgress, progress);
}

static void FetchResourcesScreen_Tick(struct LScreen* s_) {
	struct FetchResourcesScreen* s = (struct FetchResourcesScreen*)s_;
	if (!Fetcher_Working) return;

	FetchResourcesScreen_UpdateProgress(s);
	Fetcher_Update();

	if (!Fetcher_Completed) return;
	if (Fetcher_Failed)     return;

	Launcher_TryLoadTexturePack();
	CheckResourcesScreen_Next(NULL);
}

void FetchResourcesScreen_SetActive(void) {
	struct FetchResourcesScreen* s = &FetchResourcesScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = fetchResources_widgets;
	s->maxWidgets = Array_Elems(fetchResources_widgets);

	s->Activated      = FetchResourcesScreen_Activated;
	s->Tick           = FetchResourcesScreen_Tick;
	s->DrawBackground = CheckResourcesScreen_DrawBackground;
	s->ResetArea      = CheckResourcesScreen_ResetArea;

	Launcher_SetScreen((struct LScreen*)s);
}

#endif

/*########################################################################################################################*
*--------------------------------------------------------ServersScreen----------------------------------------------------*
*#########################################################################################################################*/
static struct ServersScreen {
	LScreen_Layout
	struct LInput iptSearch, iptHash;
	struct LButton btnBack, btnConnect, btnRefresh;
	struct LTable table;
	struct FontDesc rowFont;
	float tableAcc;
} ServersScreen CC_BIG_VAR;

static struct LWidget* servers_widgets[6];

LAYOUTS srv_iptSearch[] = { { ANCHOR_MIN, 10 }, { ANCHOR_MIN, 10 } };
LAYOUTS srv_iptHash[]   = { { ANCHOR_MIN, 10 }, { ANCHOR_MAX, 10 } };
LAYOUTS srv_table[5]    = { { ANCHOR_MIN, 10 }, { ANCHOR_MIN | LLAYOUT_EXTRA, 50 }, { LLAYOUT_WIDTH, 0 }, { LLAYOUT_HEIGHT, 50 } };

LAYOUTS srv_btnBack[]    = { { ANCHOR_MAX,  10 }, { ANCHOR_MIN, 10 } };
LAYOUTS srv_btnConnect[] = { { ANCHOR_MAX,  10 }, { ANCHOR_MAX, 10 } };
LAYOUTS srv_btnRefresh[] = { { ANCHOR_MAX, 135 }, { ANCHOR_MIN, 10 } };
	

static void ServersScreen_Connect(void* w) {
	struct LTable* table = &ServersScreen.table;
	cc_string* hash      = &ServersScreen.iptHash.text;

	if (!hash->length && table->rowsCount) { 
		hash = &LTable_Get(table->topRow)->hash; 
	}
	Launcher_ConnectToServer(hash);
}

static void ServersScreen_Refresh(void* w) {
	struct LButton* btn;
	if (FetchServersTask.Base.working) return;

	FetchServersTask_Run();
	btn = &ServersScreen.btnRefresh;
	LButton_SetConst(btn, "&eWorking..");
}

static void ServersScreen_HashFilter(cc_string* str) {
	int lastIndex;
	if (!str->length) return;

	/* Server url look like http://www.classicube.net/server/play/aaaaa/ */
	/* Trim it to only be the aaaaa */
	if (str->buffer[str->length - 1] == '/') str->length--;

	/* Trim the URL parts before the hash */
	lastIndex = String_LastIndexOf(str, '/');
	if (lastIndex == -1) return;
	*str = String_UNSAFE_SubstringAt(str, lastIndex + 1);
}

static void ServersScreen_SearchChanged(struct LInput* w) {
	struct ServersScreen* s = &ServersScreen;
	LTable_ApplyFilter(&s->table);
	LBackend_NeedsRedraw(&s->table);
}

static void ServersScreen_HashChanged(struct LInput* w) {
	struct ServersScreen* s = &ServersScreen;
	LTable_ShowSelected(&s->table);
	LBackend_NeedsRedraw(&s->table);
}

static void ServersScreen_OnSelectedChanged(void) {
	struct ServersScreen* s = &ServersScreen;
	LBackend_NeedsRedraw(&s->iptHash);
	LBackend_NeedsRedraw(&s->table);
}

static void ServersScreen_ReloadServers(struct ServersScreen* s) {
	int i;
	LTable_Sort(&s->table);

	for (i = 0; i < FetchServersTask.numServers; i++) 
	{
		FetchFlagsTask_Add(&FetchServersTask.servers[i]);
	}
}

static void ServersScreen_AddWidgets(struct ServersScreen* s) {
	LInput_Add(s,  &s->iptSearch, 370, "Search servers..",               srv_iptSearch);
	LInput_Add(s,  &s->iptHash,   475, "classichate.vincs.nl/server/play/...", srv_iptHash);

	LButton_Add(s, &s->btnBack,    110, 30, "Back",    
				SwitchToMain,          srv_btnBack);
	LButton_Add(s, &s->btnConnect, 130, 30, "Connect", 
				ServersScreen_Connect, srv_btnConnect);
	LButton_Add(s, &s->btnRefresh, 110, 30, "Refresh", 
				ServersScreen_Refresh, srv_btnRefresh);

	s->iptSearch.skipsEnter    = true;
	s->iptSearch.TextChanged   = ServersScreen_SearchChanged;
	s->iptHash.TextChanged     = ServersScreen_HashChanged;
	s->iptHash.ClipboardFilter = ServersScreen_HashFilter;

	s->table.filter       = &s->iptSearch.text;
	s->table.selectedHash = &s->iptHash.text;
	s->table.OnSelectedChanged = ServersScreen_OnSelectedChanged;

	if (s->table.VTABLE) {
		LScreen_AddWidget(s, &s->table);
	} else {
		LTable_Add(s, &s->table, srv_table);
	}
	LTable_Reset(&s->table);
}

static void ServersScreen_Activated(struct LScreen* s_) {
	struct ServersScreen* s = (struct ServersScreen*)s_;
	ServersScreen_AddWidgets(s);
	s->tableAcc = 0.0f;

	LInput_ClearText(&s->iptHash);
	LInput_ClearText(&s->iptSearch);

	ServersScreen_ReloadServers(s);
	/* This is so typing on keyboard by default searchs server list */
	/* But don't do that when it would cause on-screen keyboard to show */
	if (Window_Main.SoftKeyboard) return;
	LScreen_SelectWidget(s_, 0, (struct LWidget*)&s->iptSearch, false);
}

static void ServersScreen_Tick(struct LScreen* s_) {
	struct ServersScreen* s = (struct ServersScreen*)s_;
	int flagsCount;
	LScreen_Tick(s_);

	flagsCount = FetchFlagsTask.count;
	LWebTask_Tick(&FetchFlagsTask.Base, NULL);
	if (flagsCount != FetchFlagsTask.count) {
		LBackend_TableFlagAdded(&s->table);
	}

	if (!FetchServersTask.Base.working) return;
	LWebTask_Tick(&FetchServersTask.Base, NULL);
	if (!FetchServersTask.Base.completed) return;

	if (FetchServersTask.Base.success) {
		ServersScreen_ReloadServers(s);
		LBackend_NeedsRedraw(&s->table);
	}

	LButton_SetConst(&s->btnRefresh, 
				FetchServersTask.Base.success ? "Refresh" : "&cFailed");
}

static void ServersScreen_MouseWheel(struct LScreen* s_, float delta) {
	struct ServersScreen* s = (struct ServersScreen*)s_;
	s->table.VTABLE->MouseWheel(&s->table, delta);
}

static void ServersScreen_KeyDown(struct LScreen* s_, int key, cc_bool was, struct InputDevice* device) {
	struct ServersScreen* s = (struct ServersScreen*)s_;
	if (!LTable_HandlesKey(key, device)) {
		LScreen_KeyDown(s_, key, was, device);
	} else {
		s->table.VTABLE->KeyDown(&s->table, key, was, device);
	}
}

static void ServersScreen_MouseUp(struct LScreen* s_, int idx) {
	struct ServersScreen* s = (struct ServersScreen*)s_;
	s->table.VTABLE->OnUnselect(&s->table, idx);
}

void ServersScreen_SetActive(void) {
	struct ServersScreen* s = &ServersScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = servers_widgets;
	s->maxWidgets = Array_Elems(servers_widgets);

	s->Activated  = ServersScreen_Activated;
	s->Tick       = ServersScreen_Tick;
	s->MouseWheel = ServersScreen_MouseWheel;
	s->KeyDown    = ServersScreen_KeyDown;
	s->MouseUp    = ServersScreen_MouseUp;

	s->onEnterWidget  = (struct LWidget*)&s->btnConnect;
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;

	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*--------------------------------------------------------SettingsScreen---------------------------------------------------*
*#########################################################################################################################*/
static struct SettingsScreen {
	LScreen_Layout
	struct LButton btnMode, btnColours, btnBack, btnLang;
	struct LLabel  lblMode, lblColours;
	struct LCheckbox cbExtra, cbEmpty, cbScale;
	struct LLine sep;
} SettingsScreen CC_BIG_VAR;

#define SETTINGS_SCREEN_MAX_WIDGETS 10
static struct LWidget* settings_widgets[SETTINGS_SCREEN_MAX_WIDGETS];

LAYOUTS set_btnLang[]    = { { ANCHOR_CENTRE,     -135 }, { ANCHOR_CENTRE, -120 } };
LAYOUTS set_btnMode[]    = { { ANCHOR_CENTRE,     -135 }, { ANCHOR_CENTRE,  -70 } };
LAYOUTS set_lblMode[]    = { { ANCHOR_CENTRE_MIN,  -70 }, { ANCHOR_CENTRE,  -70 } };
LAYOUTS set_btnColours[] = { { ANCHOR_CENTRE,     -135 }, { ANCHOR_CENTRE,  -20 } };
LAYOUTS set_lblColours[] = { { ANCHOR_CENTRE_MIN,  -70 }, { ANCHOR_CENTRE,  -20 } };

LAYOUTS set_sep[]     = { { ANCHOR_CENTRE,        0 }, { ANCHOR_CENTRE,  15 } };
LAYOUTS set_cbExtra[] = { { ANCHOR_CENTRE_MIN, -190 }, { ANCHOR_CENTRE,  44 } };
LAYOUTS set_cbEmpty[] = { { ANCHOR_CENTRE_MIN, -190 }, { ANCHOR_CENTRE,  84 } };
LAYOUTS set_cbScale[] = { { ANCHOR_CENTRE_MIN, -190 }, { ANCHOR_CENTRE, 124 } };
LAYOUTS set_btnBack[] = { { ANCHOR_CENTRE,        0 }, { ANCHOR_CENTRE, 170 } };


#if defined CC_BUILD_MOBILE
static void SettingsScreen_LockOrientation(struct LCheckbox* w) {
	Options_SetBool(OPT_LANDSCAPE_MODE, w->value);
	Window_LockLandscapeOrientation(w->value);
	LBackend_Redraw();
}
#else
static void SettingsScreen_AutoClose(struct LCheckbox* w) {
	Options_SetBool(LOPT_AUTO_CLOSE, w->value);
}
#endif
static void SettingsScreen_ShowEmpty(struct LCheckbox* w) {
	Launcher_ShowEmptyServers = w->value;
	Options_SetBool(LOPT_SHOW_EMPTY, w->value);
}

static void SettingsScreen_DPIScaling(struct LCheckbox* w) {
#if defined CC_BUILD_WIN
	DisplayInfo.DPIScaling = w->value;
	Options_SetBool(OPT_DPI_SCALING, w->value);
	Window_ShowDialog("Restart required", "You must restart ClassiCube before display scaling takes effect");
#else
	Window_ShowDialog("Restart required", "Display scaling is currently only supported on Windows");
#endif
}

/* "Language: <current language>" - the language name is always shown in its own language */
static char set_langText[STRING_SIZE * 4];
static void SettingsScreen_SwitchLanguage(void* w) {
	Lang_SetNext();
	SettingsScreen_SetActive(); /* rebuild this screen with the new text */
}

static void SettingsScreen_AddWidgets(struct SettingsScreen* s) {
	cc_string langStr;
	String_InitArray_NT(langStr, set_langText);
	Lang_AppendTo(&langStr, "Language");
	String_AppendConst(&langStr, ": ");
	Lang_AppendStored(&langStr, Lang_CurrentName());
	set_langText[langStr.length] = '\0';

	LLine_Add(s,   &s->sep, 380, set_sep);
	LButton_Add(s, &s->btnLang, 240, 35, set_langText,
				SettingsScreen_SwitchLanguage, set_btnLang);
	LButton_Add(s, &s->btnMode, 110, 35, "Mode", 
				SwitchToChooseMode, set_btnMode);
	LLabel_Add(s,  &s->lblMode, "&eChange the enabled features", set_lblMode);

	if (!Options_GetBool(OPT_CLASSIC_MODE, false)) {
		LButton_Add(s, &s->btnColours, 110, 35, "Theme", 
					SwitchToThemes, set_btnColours);
		LLabel_Add(s,  &s->lblColours, "&eChange how the launcher looks", set_lblColours);
	}

#if defined CC_BUILD_MOBILE
	LCheckbox_Add(s, &s->cbExtra, "Force landscape", 
				SettingsScreen_LockOrientation, set_cbExtra);
#else
	LCheckbox_Add(s, &s->cbExtra, "Close this after game starts", 
				SettingsScreen_AutoClose,       set_cbExtra);
#endif

	LCheckbox_Add(s, &s->cbEmpty, "Show empty servers in list", 
				SettingsScreen_ShowEmpty,  set_cbEmpty);
	LCheckbox_Add(s, &s->cbScale, "Use display scaling", 
				SettingsScreen_DPIScaling, set_cbScale);
	LButton_Add(s,   &s->btnBack, 80, 35, "Back", 
				SwitchToOptions, set_btnBack);
}

static void SettingsScreen_Activated(struct LScreen* s_) {
	struct SettingsScreen* s = (struct SettingsScreen*)s_;
	SettingsScreen_AddWidgets(s);

#if defined CC_BUILD_MOBILE
	LCheckbox_Set(&s->cbExtra, Options_GetBool(OPT_LANDSCAPE_MODE, false));
#else
	LCheckbox_Set(&s->cbExtra, Options_GetBool(LOPT_AUTO_CLOSE, false));
#endif

	LCheckbox_Set(&s->cbEmpty, Launcher_ShowEmptyServers);
	LCheckbox_Set(&s->cbScale, DisplayInfo.DPIScaling);
}

void SettingsScreen_SetActive(void) {
	struct SettingsScreen* s = &SettingsScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = settings_widgets;
	s->maxWidgets = Array_Elems(settings_widgets);

	s->Activated      = SettingsScreen_Activated;
	s->title          = "General";
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;

	Launcher_SetScreen((struct LScreen*)s);
}
/* Alias so new code can use the cleaner name */
void GeneralScreen_SetActive(void) { SettingsScreen_SetActive(); }


/*########################################################################################################################*
*---------------------------------------------------------OptionsScreen (CCPS hub)----------------------------------------*
*#########################################################################################################################*/
static struct OptionsScreen {
	LScreen_Layout
	struct LButton btnGeneral, btnServers, btnBack;
	struct LLabel  lblGeneral, lblServers;
} OptionsScreen CC_BIG_VAR;

#define OPTIONS_SCREEN_MAX_WIDGETS 5
static struct LWidget* options_widgets[OPTIONS_SCREEN_MAX_WIDGETS];

LAYOUTS opt_btnGeneral[] = { { ANCHOR_CENTRE, -135 }, { ANCHOR_CENTRE, -50 } };
LAYOUTS opt_lblGeneral[] = { { ANCHOR_CENTRE_MIN, -70 }, { ANCHOR_CENTRE, -50 } };
LAYOUTS opt_btnServers[] = { { ANCHOR_CENTRE, -135 }, { ANCHOR_CENTRE,   0 } };
LAYOUTS opt_lblServers[] = { { ANCHOR_CENTRE_MIN, -70 }, { ANCHOR_CENTRE,   0 } };
LAYOUTS opt_btnBack[]    = { { ANCHOR_CENTRE,       0 }, { ANCHOR_CENTRE,  60 } };

static void OptionsScreen_Activated(struct LScreen* s_) {
	struct OptionsScreen* s = (struct OptionsScreen*)s_;
	LButton_Add(s, &s->btnGeneral, 130, 35, "General",
				SwitchToGeneral,  opt_btnGeneral);
	LLabel_Add(s,  &s->lblGeneral, "&eChange general settings", opt_lblGeneral);
	LButton_Add(s, &s->btnServers, 130, 35, "Servers",
				SwitchToCCPSServers, opt_btnServers);
	LLabel_Add(s,  &s->lblServers, "&eManage server list providers", opt_lblServers);
	LButton_Add(s, &s->btnBack,     80, 35, "Back",
				SwitchToMain,     opt_btnBack);
}

void OptionsScreen_SetActive(void) {
	struct OptionsScreen* s = &OptionsScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = options_widgets;
	s->maxWidgets = Array_Elems(options_widgets);

	s->Activated      = OptionsScreen_Activated;
	s->title          = "Options";
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;

	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*---------------------------------------------------------CCPSServersScreen-----------------------------------------------*
*#########################################################################################################################*/
static struct CCPSServersScreen {
	LScreen_Layout
	struct LButton btnManage, btnAdd, btnSwitch, btnBack;
	struct LLabel  lblManage, lblAdd, lblSwitch;
	struct LLabel  lblActive;
} CCPSServersScreen CC_BIG_VAR;

#define CCPSSERVERS_SCREEN_MAX_WIDGETS 8
static struct LWidget* ccpsServers_widgets[CCPSSERVERS_SCREEN_MAX_WIDGETS];

LAYOUTS cps_btnManage[] = { { ANCHOR_CENTRE, -145 }, { ANCHOR_CENTRE, -70 } };
LAYOUTS cps_lblManage[] = { { ANCHOR_CENTRE_MIN, -70 }, { ANCHOR_CENTRE, -70 } };
LAYOUTS cps_btnAdd[]    = { { ANCHOR_CENTRE, -145 }, { ANCHOR_CENTRE, -20 } };
LAYOUTS cps_lblAdd[]    = { { ANCHOR_CENTRE_MIN, -70 }, { ANCHOR_CENTRE, -20 } };
LAYOUTS cps_btnSwitch[] = { { ANCHOR_CENTRE, -145 }, { ANCHOR_CENTRE,  30 } };
LAYOUTS cps_lblSwitch[] = { { ANCHOR_CENTRE_MIN, -70 }, { ANCHOR_CENTRE,  30 } };
LAYOUTS cps_lblActive[] = { { ANCHOR_CENTRE,       0 }, { ANCHOR_CENTRE,  80 } };
LAYOUTS cps_btnBack[]   = { { ANCHOR_CENTRE,       0 }, { ANCHOR_CENTRE, 120 } };

/* Static buffer for the "Active: ..." label text */
static char cps_activeTextBuf[256];

static void CCPSServersScreen_Activated(struct LScreen* s_) {
	struct CCPSServersScreen* s = (struct CCPSServersScreen*)s_;
	cc_string activeText;

	LButton_Add(s, &s->btnManage, 160, 35, "Manage Servers",
				SwitchToManageServers, cps_btnManage);
	LLabel_Add(s,  &s->lblManage, "&eView and edit saved servers", cps_lblManage);
	LButton_Add(s, &s->btnAdd,    150, 35, "Add Server",
				SwitchToAddServer,     cps_btnAdd);
	LLabel_Add(s,  &s->lblAdd,    "&eSave a new server", cps_lblAdd);
	LButton_Add(s, &s->btnSwitch, 155, 35, "Switch Server",
				SwitchToSwitchServer,  cps_btnSwitch);
	LLabel_Add(s,  &s->lblSwitch, "&eChoose active server", cps_lblSwitch);

	/* Build active-server label into static buffer */
	activeText = String_FromRawArray(cps_activeTextBuf);
	activeText.length = 0;
	if (CCPS_ActiveServer < 0 || CCPS_ActiveServer >= CCPS_ServerCount) {
		Lang_AppendTo(&activeText, "&aActive: Default (ClassiCube)");
	} else {
		Lang_AppendTo(&activeText, "&aActive: ");
		String_AppendConst(&activeText, CCPS_Servers[CCPS_ActiveServer].displayName);
	}
	cps_activeTextBuf[activeText.length] = '\0';
	LLabel_Add(s, &s->lblActive, cps_activeTextBuf, cps_lblActive);

	LButton_Add(s, &s->btnBack, 80, 35, "Back",
				SwitchToOptions, cps_btnBack);
}

void CCPSServersScreen_SetActive(void) {
	struct CCPSServersScreen* s = &CCPSServersScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = ccpsServers_widgets;
	s->maxWidgets = Array_Elems(ccpsServers_widgets);

	s->Activated      = CCPSServersScreen_Activated;
	s->title          = "Servers";
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;

	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*---------------------------------------------------------ManageServersScreen---------------------------------------------*
*#########################################################################################################################*/
/* Dynamic per-row layouts (non-const so we can fill them at runtime) */
static struct LLayout mgr_lblLayouts[CCPS_MAX_SERVERS][2];
static struct LLayout mgr_editLayouts[CCPS_MAX_SERVERS][2];
static struct LLayout mgr_delLayouts[CCPS_MAX_SERVERS][2];

static struct ManageServersScreen {
	LScreen_Layout
	struct LLabel  lblNames[CCPS_MAX_SERVERS];
	struct LButton btnEdit[CCPS_MAX_SERVERS];
	struct LButton btnDel[CCPS_MAX_SERVERS];
	struct LLabel  lblEmpty;
	struct LButton btnBack;
} ManageServersScreen CC_BIG_VAR;

/* 8 rows * 3 widgets + lblEmpty + btnBack */
#define MANAGE_SCREEN_MAX_WIDGETS (CCPS_MAX_SERVERS * 3 + 2)
static struct LWidget* manage_widgets[MANAGE_SCREEN_MAX_WIDGETS];

LAYOUTS mgr_lblEmpty[] = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, -20 } };
LAYOUTS mgr_btnBack[]  = { { ANCHOR_CENTRE, 0 }, { ANCHOR_MAX,     10 } };

static void ManageServers_Delete(void* w) {
	struct ManageServersScreen* s = &ManageServersScreen;
	int i;
	for (i = 0; i < CCPS_MAX_SERVERS; i++) {
		if (w == &s->btnDel[i]) { CCPS_DeleteServer(i); break; }
	}
	ManageServersScreen_SetActive();
}

static void ManageServers_Edit(void* w) {
	struct ManageServersScreen* s = &ManageServersScreen;
	int i;
	for (i = 0; i < CCPS_MAX_SERVERS; i++) {
		if (w == &s->btnEdit[i]) {
			AddEditServerScreen_SetActive(i);
			return;
		}
	}
}

static void ManageServersScreen_Activated(struct LScreen* s_) {
	struct ManageServersScreen* s = (struct ManageServersScreen*)s_;
	int i, y;

	if (CCPS_ServerCount == 0) {
		LLabel_Add(s, &s->lblEmpty, "&eNo servers saved yet.", mgr_lblEmpty);
	} else {
		for (i = 0; i < CCPS_ServerCount; i++) {
			/* Row y: -90 + i*35 keeps 8 rows within the content area */
			y = -90 + i * 35;

			/* Name label — left-anchored */
			mgr_lblLayouts[i][0].type   = ANCHOR_MIN;
			mgr_lblLayouts[i][0].offset = 10;
			mgr_lblLayouts[i][1].type   = ANCHOR_CENTRE;
			mgr_lblLayouts[i][1].offset = (short)y;
			LLabel_Add(s, &s->lblNames[i], CCPS_Servers[i].displayName,
				mgr_lblLayouts[i]);

			/* Edit button — right side, secondary position */
			mgr_editLayouts[i][0].type   = ANCHOR_MAX;
			mgr_editLayouts[i][0].offset = 105;
			mgr_editLayouts[i][1].type   = ANCHOR_CENTRE;
			mgr_editLayouts[i][1].offset = (short)y;
			LButton_Add(s, &s->btnEdit[i], 85, 28, "Edit",
				ManageServers_Edit, mgr_editLayouts[i]);

			/* Delete button — far right */
			mgr_delLayouts[i][0].type   = ANCHOR_MAX;
			mgr_delLayouts[i][0].offset = 10;
			mgr_delLayouts[i][1].type   = ANCHOR_CENTRE;
			mgr_delLayouts[i][1].offset = (short)y;
			LButton_Add(s, &s->btnDel[i], 90, 28, "Delete",
				ManageServers_Delete, mgr_delLayouts[i]);
		}
	}

	LButton_Add(s, &s->btnBack, 80, 35, "Back",
				SwitchToCCPSServers, mgr_btnBack);
}

void ManageServersScreen_SetActive(void) {
	struct ManageServersScreen* s = &ManageServersScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = manage_widgets;
	s->maxWidgets = Array_Elems(manage_widgets);

	s->Activated      = ManageServersScreen_Activated;
	s->title          = "Manage Servers";
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;

	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*---------------------------------------------------------AddEditServerScreen---------------------------------------------*
*#########################################################################################################################*/
/* -1 = adding a new server, >= 0 = editing server at that index */
static int addEdit_index;

static struct AddEditServerScreen {
	LScreen_Layout
	struct LLabel  lblName, lblUrl, lblPort, lblStatus;
	struct LInput  iptName, iptUrl, iptPort;
	struct LButton btnSave, btnCancel;
} AddEditServerScreen CC_BIG_VAR;

#define ADDEDIT_SCREEN_MAX_WIDGETS 9
static struct LWidget* addEdit_widgets[ADDEDIT_SCREEN_MAX_WIDGETS];

LAYOUTS ae_lblName[]   = { { ANCHOR_CENTRE_MIN, -185 }, { ANCHOR_CENTRE, -90 } };
LAYOUTS ae_iptName[]   = { { ANCHOR_CENTRE_MIN, -140 }, { ANCHOR_CENTRE, -65 } };
LAYOUTS ae_lblUrl[]    = { { ANCHOR_CENTRE_MIN, -185 }, { ANCHOR_CENTRE, -20 } };
LAYOUTS ae_iptUrl[]    = { { ANCHOR_CENTRE_MIN, -140 }, { ANCHOR_CENTRE,   5 } };
LAYOUTS ae_lblPort[]   = { { ANCHOR_CENTRE_MIN, -185 }, { ANCHOR_CENTRE,  50 } };
LAYOUTS ae_iptPort[]   = { { ANCHOR_CENTRE_MIN, -140 }, { ANCHOR_CENTRE,  75 } };
LAYOUTS ae_lblStatus[] = { { ANCHOR_CENTRE,       0  }, { ANCHOR_CENTRE, 115 } };
LAYOUTS ae_btnSave[]   = { { ANCHOR_CENTRE,  -60  }, { ANCHOR_CENTRE, 145 } };
LAYOUTS ae_btnCancel[] = { { ANCHOR_CENTRE,   60  }, { ANCHOR_CENTRE, 145 } };

static void AddEditServer_Save(void* w) {
	struct AddEditServerScreen* s = &AddEditServerScreen;
	cc_string name, url, port;

	LInput_UNSAFE_GetText(&s->iptName, &name);
	LInput_UNSAFE_GetText(&s->iptUrl,  &url);
	LInput_UNSAFE_GetText(&s->iptPort, &port);

	if (!name.length) {
		LLabel_SetConst(&s->lblStatus, "&cDisplay name required"); return;
	}
	if (!url.length) {
		LLabel_SetConst(&s->lblStatus, "&cURL required"); return;
	}

	if (addEdit_index < 0) {
		if (!CCPS_AddServer(&name, &url, &port)) {
			LLabel_SetConst(&s->lblStatus, "&cMax servers reached (8)"); return;
		}
	} else {
		CCPS_UpdateServer(addEdit_index, &name, &url, &port);
	}
	ManageServersScreen_SetActive();
}

static void AddEditServer_Cancel(void* w) {
	ManageServersScreen_SetActive();
}

static void AddEditServerScreen_Activated(struct LScreen* s_) {
	struct AddEditServerScreen* s = (struct AddEditServerScreen*)s_;

	LLabel_Add(s,  &s->lblName,   "Display name",  ae_lblName);
	LInput_Add(s,  &s->iptName,   280, "e.g. My Private Server", ae_iptName);
	LLabel_Add(s,  &s->lblUrl,    "URL / IP",       ae_lblUrl);
	LInput_Add(s,  &s->iptUrl,    280, "e.g. https://example.com/api", ae_iptUrl);
	LLabel_Add(s,  &s->lblPort,   "Port (info only)", ae_lblPort);
	LInput_Add(s,  &s->iptPort,    80, "e.g. 25565",   ae_iptPort);
	LLabel_Add(s,  &s->lblStatus, "",               ae_lblStatus);
	LButton_Add(s, &s->btnSave,   100, 35, "Save",   AddEditServer_Save,   ae_btnSave);
	LButton_Add(s, &s->btnCancel, 100, 35, "Cancel", AddEditServer_Cancel, ae_btnCancel);

	/* Pre-fill if editing */
	if (addEdit_index >= 0 && addEdit_index < CCPS_ServerCount) {
		struct CCPSServer* srv = &CCPS_Servers[addEdit_index];
		cc_string v;
		v = String_FromRawArray(srv->displayName); LInput_SetText(&s->iptName, &v);
		v = String_FromRawArray(srv->url);         LInput_SetText(&s->iptUrl,  &v);
		v = String_FromRawArray(srv->port);        LInput_SetText(&s->iptPort, &v);
	}
}

void AddEditServerScreen_SetActive(int editIndex) {
	struct AddEditServerScreen* s = &AddEditServerScreen;
	addEdit_index = editIndex;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = addEdit_widgets;
	s->maxWidgets = Array_Elems(addEdit_widgets);

	s->Activated      = AddEditServerScreen_Activated;
	s->title          = (editIndex < 0) ? "Add Server" : "Edit Server";
	s->onEnterWidget  = (struct LWidget*)&s->btnSave;
	s->onEscapeWidget = (struct LWidget*)&s->btnCancel;

	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*---------------------------------------------------------SwitchServerScreen----------------------------------------------*
*#########################################################################################################################*/
static struct LLayout sw_btnLayouts[CCPS_MAX_SERVERS + 1][2]; /* +1 for Default */

static struct SwitchServerScreen {
	LScreen_Layout
	struct LButton btnDefault;
	struct LButton btnServers[CCPS_MAX_SERVERS];
	struct LButton btnBack;
} SwitchServerScreen CC_BIG_VAR;

/* Default + 8 servers + Back */
#define SWITCH_SCREEN_MAX_WIDGETS (CCPS_MAX_SERVERS + 2)
static struct LWidget* switch_widgets[SWITCH_SCREEN_MAX_WIDGETS];

LAYOUTS sw_btnBack[] = { { ANCHOR_CENTRE, 0 }, { ANCHOR_MAX, 10 } };

static void SwitchServer_SetDefault(void* w) {
	CCPS_SetActive(-1);
	SwitchServerScreen_SetActive();
}

static void SwitchServer_SetCustom(void* w) {
	struct SwitchServerScreen* s = &SwitchServerScreen;
	int i;
	for (i = 0; i < CCPS_MAX_SERVERS; i++) {
		if (w == &s->btnServers[i]) { CCPS_SetActive(i); break; }
	}
	SwitchServerScreen_SetActive();
}

/* Per-slot static label buffers for "> active <" marking */
static char sw_markedBuf[CCPS_MAX_SERVERS][128];

static void SwitchServerScreen_Activated(struct LScreen* s_) {
	struct SwitchServerScreen* s = (struct SwitchServerScreen*)s_;
	const char* label;
	cc_string marked;
	int i, y;

	/* Default (ClassiCube) button — at top */
	sw_btnLayouts[0][0].type   = ANCHOR_CENTRE;
	sw_btnLayouts[0][0].offset = 0;
	sw_btnLayouts[0][1].type   = ANCHOR_CENTRE;
	sw_btnLayouts[0][1].offset = -90;
	label = (CCPS_ActiveServer < 0) ? "> Default (ClassiCube) <" : "Default (ClassiCube)";
	LButton_Add(s, &s->btnDefault, 270, 32, label,
				SwitchServer_SetDefault, sw_btnLayouts[0]);

	for (i = 0; i < CCPS_ServerCount; i++) {
		y = -90 + (i + 1) * 40;
		sw_btnLayouts[i + 1][0].type   = ANCHOR_CENTRE;
		sw_btnLayouts[i + 1][0].offset = 0;
		sw_btnLayouts[i + 1][1].type   = ANCHOR_CENTRE;
		sw_btnLayouts[i + 1][1].offset = (short)y;

		if (CCPS_ActiveServer == i) {
			/* Build "> Name <" into persistent static buffer */
			marked = String_FromRawArray(sw_markedBuf[i]);
			marked.length = 0;
			String_AppendConst(&marked, "> ");
			String_AppendConst(&marked, CCPS_Servers[i].displayName);
			String_AppendConst(&marked, " <");
			sw_markedBuf[i][marked.length] = '\0';
			label = sw_markedBuf[i];
		} else {
			label = CCPS_Servers[i].displayName;
		}

		LButton_Add(s, &s->btnServers[i], 270, 32, label,
					SwitchServer_SetCustom, sw_btnLayouts[i + 1]);
	}

	LButton_Add(s, &s->btnBack, 80, 35, "Back",
				SwitchToCCPSServers, sw_btnBack);
}

void SwitchServerScreen_SetActive(void) {
	struct SwitchServerScreen* s = &SwitchServerScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = switch_widgets;
	s->maxWidgets = Array_Elems(switch_widgets);

	s->Activated      = SwitchServerScreen_Activated;
	s->title          = "Switch Server";
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;

	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*----------------------------------------------------------ThemesScreen----------------------------------------------------*
*#########################################################################################################################*/
static struct ThemesScreen {
	LScreen_Layout
	struct LButton btnModern, btnClassic, btnNordic;
	struct LButton btnCustom, btnBack;
} ThemesScreen CC_BIG_VAR;

#define THEME_SCREEN_MAX_WIDGETS 5
static struct LWidget* themes_widgets[THEME_SCREEN_MAX_WIDGETS];

LAYOUTS the_btnModern[]  = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, -120 } };
LAYOUTS the_btnClassic[] = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  -70 } };
LAYOUTS the_btnNordic[]  = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  -20 } };
LAYOUTS the_btnCustom[]  = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  120 } };
LAYOUTS the_btnBack[]    = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  170 } };


static void ThemesScreen_Set(const struct LauncherTheme* theme) {
	Launcher_Theme = *theme;
	Launcher_SaveTheme();
	LBackend_ThemeChanged();
}

static void ThemesScreen_Modern(void* w) {
	ThemesScreen_Set(&Launcher_ModernTheme);
}
static void ThemesScreen_Classic(void* w) {
	ThemesScreen_Set(&Launcher_ClassicTheme);
}
static void ThemesScreen_Nordic(void* w) {
	ThemesScreen_Set(&Launcher_NordicTheme);
}

static void ThemesScreen_Activated(struct LScreen* s_) {
	struct ThemesScreen* s = (struct ThemesScreen*)s_;

	LButton_Add(s, &s->btnModern,  200, 35, "Modern",  
				ThemesScreen_Modern,  the_btnModern);
	LButton_Add(s, &s->btnClassic, 200, 35, "Classic", 
				ThemesScreen_Classic, the_btnClassic);
	LButton_Add(s, &s->btnNordic,  200, 35, "Nordic",  
				ThemesScreen_Nordic,  the_btnNordic);
	LButton_Add(s, &s->btnCustom,  200, 35, "Custom",  
				SwitchToColours,      the_btnCustom);
	LButton_Add(s, &s->btnBack,     80, 35, "Back",    
				SwitchToSettings,     the_btnBack);
}

void ThemesScreen_SetActive(void) {
	struct ThemesScreen* s = &ThemesScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = themes_widgets;
	s->maxWidgets = Array_Elems(themes_widgets);

	s->Activated      = ThemesScreen_Activated;
	s->title          = "Select theme";
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;

	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*--------------------------------------------------------UpdatesScreen----------------------------------------------------*
*#########################################################################################################################*/
static struct UpdatesScreen {
	LScreen_Layout
	struct LLine seps[2];
	struct LButton btnRel[2], btnDev[2], btnBack;
	struct LLabel  lblYour, lblRel, lblDev, lblInfo, lblStatus;
	int buildProgress, buildIndex;
	cc_bool pendingFetch, release;
} UpdatesScreen CC_BIG_VAR;

#define UPDATESSCREEN_MAX_WIDGETS 12
static struct LWidget* updates_widgets[UPDATESSCREEN_MAX_WIDGETS];

LAYOUTS upd_lblYour[] = { { ANCHOR_CENTRE, -5 }, { ANCHOR_CENTRE, -120 } };
LAYOUTS upd_seps0[]   = { { ANCHOR_CENTRE,  0 }, { ANCHOR_CENTRE, -100 } };
LAYOUTS upd_seps1[]   = { { ANCHOR_CENTRE,  0 }, { ANCHOR_CENTRE,   -5 } };

LAYOUTS upd_lblRel[]    = { { ANCHOR_CENTRE, -20 }, { ANCHOR_CENTRE, -75 } };
LAYOUTS upd_lblDev[]    = { { ANCHOR_CENTRE, -30 }, { ANCHOR_CENTRE,  20 } };
LAYOUTS upd_lblInfo[]   = { { ANCHOR_CENTRE,   0 }, { ANCHOR_CENTRE, 105 } };
LAYOUTS upd_lblStatus[] = { { ANCHOR_CENTRE,   0 }, { ANCHOR_CENTRE, 130 } };
LAYOUTS upd_btnBack[]   = { { ANCHOR_CENTRE,   0 }, { ANCHOR_CENTRE, 170 } };

/* Update button layouts when 1 build */
LAYOUTS upd_btnRel0_1[] = { { ANCHOR_CENTRE,   0 }, { ANCHOR_CENTRE, -40 } };
LAYOUTS upd_btnDev0_1[] = { { ANCHOR_CENTRE,   0 }, { ANCHOR_CENTRE,  55 } };
/* Update button layouts when 2 builds */
LAYOUTS upd_btnRel0_2[] = { { ANCHOR_CENTRE, -80 }, { ANCHOR_CENTRE, -40 } };
LAYOUTS upd_btnRel1_2[] = { { ANCHOR_CENTRE,  80 }, { ANCHOR_CENTRE, -40 } };
LAYOUTS upd_btnDev0_2[] = { { ANCHOR_CENTRE, -80 }, { ANCHOR_CENTRE,  55 } };
LAYOUTS upd_btnDev1_2[] = { { ANCHOR_CENTRE,  80 }, { ANCHOR_CENTRE,  55 } };


CC_NOINLINE static void UpdatesScreen_FormatTime(cc_string* str, int delta) {
	const char* span;
	int unit, value = Math_AbsI(delta);
	cc_bool plural;

	/* checked on the raw value so each language can supply its own singular/plural words */
	if (value < SECS_PER_MIN) {
		unit = 1;
	} else if (value < SECS_PER_HOUR) {
		unit = SECS_PER_MIN;
	} else if (value < SECS_PER_DAY) {
		unit = SECS_PER_HOUR;
	} else {
		unit = SECS_PER_DAY;
	}

	value /= unit;
	plural = value > 1;
	if (unit == 1)                 span = plural ? "seconds" : "second";
	else if (unit == SECS_PER_MIN) span = plural ? "minutes" : "minute";
	else if (unit == SECS_PER_HOUR) span = plural ? "hours"  : "hour";
	else                           span = plural ? "days"    : "day";

	String_AppendInt(str, value);
	String_Append(str, ' ');
	Lang_AppendTo(str, span);
	Lang_AppendTo(str, delta >= 0 ? " ago" : " in the future");
}

static void UpdatesScreen_Format(struct LLabel* lbl, const char* prefix, cc_uint64 timestamp) {
	cc_string str; char buffer[STRING_SIZE * 4];
	TimeMS now;
	int delta;

	String_InitArray(str, buffer);
	Lang_AppendTo(&str, prefix);

	if (!timestamp) {
		Lang_AppendTo(&str, "&cCheck failed");
	} else {
		now   = DateTime_CurrentUTC() - UNIX_EPOCH_SECONDS;
		delta = (int)(now - timestamp);
		UpdatesScreen_FormatTime(&str, delta);
	}
	LLabel_SetText(lbl, &str);
}

static void UpdatesScreen_FormatBoth(struct UpdatesScreen* s) {
	UpdatesScreen_Format(&s->lblRel, "Latest release: ",   CheckUpdateTask.relTimestamp);
	UpdatesScreen_Format(&s->lblDev, "Latest dev build: ", CheckUpdateTask.devTimestamp);
}

static void UpdatesScreen_UpdateHeader(struct UpdatesScreen* s, cc_string* str) {
	const char* message = s->release ? "release " : "dev build ";

	Lang_AppendTo(str, "&eFetching latest ");
	Lang_AppendTo(str, message);
	String_AppendConst(str, Updater_Info.builds[s->buildIndex].name);
}

static void UpdatesScreen_DoFetch(struct UpdatesScreen* s) {
	cc_string str; char strBuffer[STRING_SIZE * 4];
	cc_uint64 time;
	
	time = s->release ? CheckUpdateTask.relTimestamp : CheckUpdateTask.devTimestamp;
	if (!time || FetchUpdateTask.Base.working) return;
	FetchUpdateTask_Run(s->release, s->buildIndex);

	s->pendingFetch  = false;
	s->buildProgress = -1;
	String_InitArray(str, strBuffer);

	UpdatesScreen_UpdateHeader(s, &str);
	String_AppendConst(&str, "..");
	LLabel_SetText(&s->lblStatus, &str);
}

static void UpdatesScreen_Get(cc_bool release, int buildIndex) {
	struct UpdatesScreen* s = &UpdatesScreen;
	/* This code is deliberately split up to handle this particular case: */
	/*  The user clicked this button before CheckUpdateTask completed, */
	/*  therefore update fetching would not actually occur. (as timestamp is 0) */
	/*  By storing requested build, it can be fetched later upon completion. */
	s->pendingFetch = true;
	s->release      = release;
	s->buildIndex   = buildIndex;
	UpdatesScreen_DoFetch(s);
}

static void UpdatesScreen_CheckTick(struct UpdatesScreen* s) {
	if (!CheckUpdateTask.Base.working) return;
	LWebTask_Tick(&CheckUpdateTask.Base, NULL);

	if (!CheckUpdateTask.Base.completed) return;
	UpdatesScreen_FormatBoth(s);
	if (s->pendingFetch) UpdatesScreen_DoFetch(s);
}

static void UpdatesScreen_UpdateProgress(struct UpdatesScreen* s, struct LWebTask* task) {
	cc_string str; char strBuffer[STRING_SIZE * 4];
	int progress = Http_CheckProgress(task->reqID);
	if (progress == s->buildProgress) return;

	s->buildProgress = progress;
	if (progress < 0 || progress > 100) return;
	String_InitArray(str, strBuffer);

	UpdatesScreen_UpdateHeader(s, &str);
	String_Format1(&str, " &a%i%%", &s->buildProgress);
	LLabel_SetText(&s->lblStatus, &str);
}

static void FetchUpdatesError(struct HttpRequest* req) {
	cc_string str; char strBuffer[STRING_SIZE * 4];
	String_InitArray(str, strBuffer);

	Launcher_DisplayHttpError(req, "fetching update", &str);
	LLabel_SetText(&UpdatesScreen.lblStatus, &str);
}

static void UpdatesScreen_FetchTick(struct UpdatesScreen* s) {
	if (!FetchUpdateTask.Base.working) return;

	LWebTask_Tick(&FetchUpdateTask.Base, FetchUpdatesError);
	UpdatesScreen_UpdateProgress(s, &FetchUpdateTask.Base);
	if (!FetchUpdateTask.Base.completed) return;

	if (!FetchUpdateTask.Base.success) return;
	/* FetchUpdateTask handles saving the updated file for us */
	Launcher_ShouldExit   = true;
	Launcher_ShouldUpdate = true;
}

static void UpdatesScreen_Rel_0(void* w) { UpdatesScreen_Get(true,  0); }
static void UpdatesScreen_Rel_1(void* w) { UpdatesScreen_Get(true,  1); }
static void UpdatesScreen_Dev_0(void* w) { UpdatesScreen_Get(false, 0); }
static void UpdatesScreen_Dev_1(void* w) { UpdatesScreen_Get(false, 1); }

static void UpdatesScreen_AddWidgets(struct UpdatesScreen* s) {
	int builds = Updater_Info.numBuilds;

	LLine_Add(s,   &s->seps[0],   320,                   upd_seps0);
	LLine_Add(s,   &s->seps[1],   320,                   upd_seps1);
	LLabel_Add(s,  &s->lblYour, "Your build: (unknown)", upd_lblYour);

	LLabel_Add(s,  &s->lblRel, "Latest release: Checking..",   upd_lblRel);
	LLabel_Add(s,  &s->lblDev, "Latest dev build: Checking..", upd_lblDev);
	LLabel_Add(s,  &s->lblStatus, "",              upd_lblStatus);
	LLabel_Add(s,  &s->lblInfo, Updater_Info.info, upd_lblInfo);
	LButton_Add(s, &s->btnBack, 80, 35, "Back", 
				SwitchToMain, upd_btnBack);

	if (builds >= 1) {
		LButton_Add(s, &s->btnRel[0], 130, 35, Updater_Info.builds[0].name,
							UpdatesScreen_Rel_0, builds == 1 ? upd_btnRel0_1 : upd_btnRel0_2);
		LButton_Add(s, &s->btnDev[0], 130, 35, Updater_Info.builds[0].name,
							UpdatesScreen_Dev_0, builds == 1 ? upd_btnDev0_1 : upd_btnDev0_2);
	}

	if (builds >= 2) {
		LButton_Add(s, &s->btnRel[1], 130, 35, Updater_Info.builds[1].name, 
					UpdatesScreen_Rel_1, upd_btnRel1_2);
		LButton_Add(s, &s->btnDev[1], 130, 35, Updater_Info.builds[1].name, 
					UpdatesScreen_Dev_1, upd_btnDev1_2);
	}
}

static void UpdatesScreen_Activated(struct LScreen* s_) {
	struct UpdatesScreen* s = (struct UpdatesScreen*)s_;
	cc_uint64 buildTime;
	cc_result res;
	UpdatesScreen_AddWidgets(s);

	/* Initially fill out with update check result from main menu */
	if (CheckUpdateTask.Base.completed && CheckUpdateTask.Base.success) {
		UpdatesScreen_FormatBoth(s);
	}
	CheckUpdateTask_Run();
	s->pendingFetch = false;

	res = Updater_GetBuildTime(&buildTime);
	if (res) { Logger_SysWarn(res, "getting build time"); return; }
	UpdatesScreen_Format(&s->lblYour, "Your build: ", buildTime);
}

static void UpdatesScreen_Tick(struct LScreen* s_) {
	struct UpdatesScreen* s = (struct UpdatesScreen*)s_;
	LScreen_Tick(s_);

	UpdatesScreen_FetchTick(s);
	UpdatesScreen_CheckTick(s);
}

/* Aborts fetch if it is in progress */
static void UpdatesScreen_Deactivated(struct LScreen* s_) {
	struct UpdatesScreen* s = (struct UpdatesScreen*)s_;
	s->buildProgress = -1;

	FetchUpdateTask.Base.working = false;
	s->lblStatus.text.length     = 0;
}

void UpdatesScreen_SetActive(void) {
	struct UpdatesScreen* s = &UpdatesScreen;
	LScreen_Reset((struct LScreen*)s);

	s->widgets    = updates_widgets;
	s->maxWidgets = Array_Elems(updates_widgets);

	s->Activated      = UpdatesScreen_Activated;
	s->Tick           = UpdatesScreen_Tick;
	s->Deactivated    = UpdatesScreen_Deactivated;
	s->title          = "Update game";
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;

	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*-------------------------------------------------FriendsHubScreen--------------------------------------------------------*
*#########################################################################################################################*/
static struct FriendsHubScreen {
	LScreen_Layout
	struct LButton btnFriendsList, btnAddFriend, btnRequests, btnBack;
} FriendsHubScreen CC_BIG_VAR;

static struct LWidget* friendsHub_widgets[3];

LAYOUTS fhub_btnList[]     = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, -60 } };
LAYOUTS fhub_btnAdd[]      = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,   0 } };
LAYOUTS fhub_btnRequests[] = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  60 } };
LAYOUTS fhub_btnBack[]     = { { ANCHOR_CENTRE, 0 }, { ANCHOR_MAX,     30 } };

static void FriendsHubScreen_Activated(struct LScreen* s_) {
	struct FriendsHubScreen* s = (struct FriendsHubScreen*)s_;
	LButton_Add(s, &s->btnAddFriend,   240, 35, "Add Friend",
				SwitchToAddFriendScreen,   fhub_btnAdd);
	LButton_Add(s, &s->btnRequests,    240, 35, "Friend Requests",
				SwitchToFriendRequestsScreen, fhub_btnRequests);
	LButton_Add(s, &s->btnBack,        200, 35, "Back",
				SwitchToMain, fhub_btnBack);
}

void FriendsHubScreen_SetActive(void) {
	struct FriendsHubScreen* s = &FriendsHubScreen;
	if (!Friends_HasUsername() && Launcher_Username.length) Friends_Login(&Launcher_Username);
	LScreen_Reset((struct LScreen*)s);
	s->widgets    = friendsHub_widgets;
	s->maxWidgets = Array_Elems(friendsHub_widgets);
	s->Activated  = FriendsHubScreen_Activated;
	s->title      = "Friends";
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;
	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*-----------------------------------------------FriendsListScreen---------------------------------------------------------*
*#########################################################################################################################*/
#define FLIST_MAX 8
static struct FriendsListScreen {
	LScreen_Layout
	struct LButton entries[FLIST_MAX];
	struct LButton btnLeft, btnRight, btnBack;
	struct LLabel  lblStatus;
	char entryBuf[FLIST_MAX][80];
	int page;
	int lastVersion;
} FriendsListScreen CC_BIG_VAR;

static struct LWidget* friendsList_widgets[FLIST_MAX + 4];

LAYOUTS fl_lblStatus[] = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, -130 } };
LAYOUTS fl_btnLeft[]   = { { ANCHOR_CENTRE, -120 }, { ANCHOR_MAX, 80 } };
LAYOUTS fl_btnRight[]  = { { ANCHOR_CENTRE,  120 }, { ANCHOR_MAX, 80 } };
LAYOUTS fl_btnBack[]   = { { ANCHOR_CENTRE,    0 }, { ANCHOR_MAX, 30 } };

static struct LLayout fl_entries[FLIST_MAX][2] = {
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, -100 }},
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  -60 }},
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  -20 }},
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,   20 }},
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,   60 }},
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  100 }},
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  140 }},
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  180 }},
};

static void FriendsListScreen_PageLeft(void* w)  {
	if (FriendsListScreen.page > 0) FriendsListScreen.page--;
	FriendsListScreen_SetActive();
}
static void FriendsListScreen_PageRight(void* w) {
	FriendsListScreen.page++;
	FriendsListScreen_SetActive();
}

static void FriendsListScreen_EntryClick(void* widget) {
	struct LButton* btn = (struct LButton*)widget;
	cc_string name = btn->text;
	if (name.length >= 2 && name.buffer[0] == '&') {
		name = String_UNSAFE_Substring(&name, 2, name.length - 2);
	}
	FriendManageScreen_SetActive(&name);
}

static void FriendsListScreen_Activated(struct LScreen* s_) {
	struct FriendsListScreen* s = (struct FriendsListScreen*)s_;
	int start = s->page * FLIST_MAX;
	int i, shown = 0;
	cc_string text;

	if (!Friends_ListVersion) Friends_FetchList();

	if (!Friends_ListVersion)
		LLabel_Add(s, &s->lblStatus, "Loading...", fl_lblStatus);
	else
		LLabel_Add(s, &s->lblStatus, "", fl_lblStatus);

	for (i = start; i < Friends_Count && shown < FLIST_MAX; i++, shown++) {
		struct FriendEntry* e = &Friends_List[i];
		String_InitArray(text, s->entryBuf[shown]);
		/* green = online, white = offline */
		if (e->status == FRIEND_ONLINE)
			String_Format1(&text, "&a%s", &e->name);
		else
			String_AppendString(&text, &e->name);
		LButton_Add(s, &s->entries[shown], 300, 30,
					text.buffer, FriendsListScreen_EntryClick,
					fl_entries[shown]);
	}
	if (Friends_ListVersion == 0)
		LLabel_SetConst(&s->lblStatus, "Loading...");
	else if (Friends_Count == 0)
		LLabel_SetConst(&s->lblStatus, "No friends yet!");
	else
		LLabel_SetConst(&s->lblStatus, "");

	if (start > 0)
		LButton_Add(s, &s->btnLeft,  60, 30, "<", FriendsListScreen_PageLeft,  fl_btnLeft);
	if (Friends_Count > start + FLIST_MAX)
		LButton_Add(s, &s->btnRight, 60, 30, ">", FriendsListScreen_PageRight, fl_btnRight);
	LButton_Add(s, &s->btnBack, 200, 35, "Back", SwitchToFriends, fl_btnBack);
}

static void FriendsListScreen_Tick(struct LScreen* s_) {
	struct FriendsListScreen* s = (struct FriendsListScreen*)s_;
	if (s->lastVersion == Friends_ListVersion) return;
	s->lastVersion = Friends_ListVersion;
	if (Friends_ListVersion) FriendsListScreen_SetActive();
}

void FriendsListScreen_SetActive(void) {
	struct FriendsListScreen* s = &FriendsListScreen;
	LScreen_Reset((struct LScreen*)s);
	s->widgets    = friendsList_widgets;
	s->maxWidgets = Array_Elems(friendsList_widgets);
	s->Activated  = FriendsListScreen_Activated;
	s->Tick       = FriendsListScreen_Tick;
	s->title      = "Friends";
	s->lastVersion = Friends_ListVersion;
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;
	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*-----------------------------------------------FriendManageScreen--------------------------------------------------------*
*#########################################################################################################################*/
static struct FriendManageScreen {
	LScreen_Layout
	struct LButton btnUnfriend, btnJoin, btnBack;
	struct LLabel  lblName;
	char targetBuf[64];
	cc_string target;
} FriendManageScreen CC_BIG_VAR;

static struct LWidget* friendManage_widgets[4];
LAYOUTS fm_lblName[]    = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, -80 } };
LAYOUTS fm_btnUnfriend[]= { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, -20 } };
LAYOUTS fm_btnJoin[]    = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  30 } };
LAYOUTS fm_btnBack[]    = { { ANCHOR_CENTRE, 0 }, { ANCHOR_MAX,     30 } };

static void FriendManageScreen_Unfriend(void* w) {
	Friends_Remove(&FriendManageScreen.target);
	FriendsListScreen_SetActive();
}

static void FriendManageScreen_Join(void* w) {
	/* Find the friend's server string */
	int i;
	for (i = 0; i < Friends_Count; i++) {
		if (String_Equals(&Friends_List[i].name, &FriendManageScreen.target)) {
			cc_string srv = Friends_List[i].server;
			if (srv.length) {
				/* server is "ip:port" - parse and connect */
				cc_string ip, portStr;
				char portBuf[8] = "25565";
				int colon = String_IndexOf(&srv, ':');
				if (colon >= 0) {
					ip      = String_UNSAFE_Substring(&srv, 0, colon);
					portStr = String_UNSAFE_Substring(&srv, colon + 1,
													  srv.length - colon - 1);
					Mem_Copy(portBuf, portStr.buffer, portStr.length);
					portBuf[portStr.length] = '\0';
				} else { ip = srv; }
				{
					cc_string portCC = String_FromReadonly(portBuf);
					Launcher_StartGame(&Launcher_Username, &String_Empty,
									   &ip, &portCC, &String_Empty, 1);
				}
			}
			break;
		}
	}
}

static void FriendManageScreen_Activated(struct LScreen* s_) {
	struct FriendManageScreen* s = (struct FriendManageScreen*)s_;
	int i; cc_bool online = false; cc_bool inServer = false;
	char nameLbl[256]; cc_string nameLblStr;
	String_InitArray(nameLblStr, nameLbl);
	String_AppendString(&nameLblStr, &s->target);

	for (i = 0; i < Friends_Count; i++) {
		if (String_Equals(&Friends_List[i].name, &s->target)) {
			online   = (Friends_List[i].status == FRIEND_ONLINE);
			inServer = online && Friends_List[i].server.length > 0;
			break;
		}
	}
	if (online) Lang_AppendTo(&nameLblStr, " &a(Online)");
	else        Lang_AppendTo(&nameLblStr, " &7(Offline)");

	LLabel_Add(s,  &s->lblName,    nameLblStr.buffer,   fm_lblName);
	LButton_Add(s, &s->btnUnfriend, 200, 35, "Unfriend",
				FriendManageScreen_Unfriend, fm_btnUnfriend);
	if (inServer)
		LButton_Add(s, &s->btnJoin, 200, 35, "Join Friend's Server",
					FriendManageScreen_Join, fm_btnJoin);
	LButton_Add(s, &s->btnBack, 200, 35, "Back",
				SwitchToFriendsListScreen, fm_btnBack);
}

void FriendManageScreen_SetActive(const cc_string* target) {
	struct FriendManageScreen* s = &FriendManageScreen;
	LScreen_Reset((struct LScreen*)s);
	s->widgets    = friendManage_widgets;
	s->maxWidgets = Array_Elems(friendManage_widgets);
	s->Activated  = FriendManageScreen_Activated;
	s->title      = "Manage Friend";

	Mem_Copy(s->targetBuf, target->buffer,
			 target->length < 63 ? target->length : 63);
	s->targetBuf[target->length < 63 ? target->length : 63] = '\0';
	s->target = String_FromReadonly(s->targetBuf);

	s->onEscapeWidget = (struct LWidget*)&s->btnBack;
	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*-----------------------------------------------AddFriendScreen-----------------------------------------------------------*
*#########################################################################################################################*/
static struct AddFriendScreen {
	LScreen_Layout
	struct LInput  iptName;
	struct LButton btnSend, btnBack;
	struct LLabel  lblStatus;
} AddFriendScreen CC_BIG_VAR;

static struct LWidget* addFriend_widgets[4];
LAYOUTS af_iptName[]   = { { ANCHOR_CENTRE_MIN, -140 }, { ANCHOR_CENTRE, -30 } };
LAYOUTS af_btnSend[]   = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, 30 } };
LAYOUTS af_lblStatus[] = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, 70 } };
LAYOUTS af_btnBack[]   = { { ANCHOR_CENTRE, 0 }, { ANCHOR_MAX,    30 } };

static void AddFriendScreen_Send(void* w) {
	struct AddFriendScreen* s = &AddFriendScreen;
	cc_string name = s->iptName.text;
	int i;
	if (!name.length) {
		LLabel_SetConst(&s->lblStatus, "&cEnter a username first"); return;
	}
	/* No spaces allowed */
	for (i = 0; i < name.length; i++) {
		if (name.buffer[i] == ' ') {
			LLabel_SetConst(&s->lblStatus, "&cNo spaces in username"); return;
		}
	}
	Friends_SendRequest(&name);
	LLabel_SetConst(&s->lblStatus, "&aRequest sent!");
	LInput_ClearText(&s->iptName);
}

static void AddFriendScreen_Activated(struct LScreen* s_) {
	struct AddFriendScreen* s = (struct AddFriendScreen*)s_;
	LInput_Add(s,  &s->iptName,   280, "Username (case sensitive)..", af_iptName);
	LButton_Add(s, &s->btnSend,   200, 35, "Send Request",
				AddFriendScreen_Send, af_btnSend);
	LLabel_Add(s,  &s->lblStatus, "",  af_lblStatus);
	LButton_Add(s, &s->btnBack,   200, 35, "Back",
				SwitchToFriends, af_btnBack);
}

void AddFriendScreen_SetActive(void) {
	struct AddFriendScreen* s = &AddFriendScreen;
	LScreen_Reset((struct LScreen*)s);
	s->widgets    = addFriend_widgets;
	s->maxWidgets = Array_Elems(addFriend_widgets);
	s->Activated  = AddFriendScreen_Activated;
	s->title      = "Add Friend";
	s->onEnterWidget  = (struct LWidget*)&s->btnSend;
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;
	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*---------------------------------------------FriendRequestsScreen--------------------------------------------------------*
*#########################################################################################################################*/
#define FREQ_MAX 8
static struct FriendRequestsScreen {
	LScreen_Layout
	struct LButton entries[FREQ_MAX];
	struct LButton btnBack;
	struct LLabel  lblStatus;
	int page;
	int lastVersion;
} FriendRequestsScreen CC_BIG_VAR;

static struct LWidget* friendReq_widgets[FREQ_MAX + 2];
LAYOUTS fr_lblStatus[] = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, -130 } };
LAYOUTS fr_btnBack[]   = { { ANCHOR_CENTRE, 0 }, { ANCHOR_MAX,     30 } };
static struct LLayout fr_entries[FREQ_MAX][2] = {
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, -100 }},
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  -60 }},
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  -20 }},
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,   20 }},
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,   60 }},
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  100 }},
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  140 }},
	{{ ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE,  180 }},
};

static void FriendRequestsScreen_EntryClick(void* widget) {
	struct LButton* btn = (struct LButton*)widget;
	FriendRequestManageScreen_SetActive(&btn->text);
}

static void FriendRequestsScreen_Activated(struct LScreen* s_) {
	struct FriendRequestsScreen* s = (struct FriendRequestsScreen*)s_;
	int start = s->page * FREQ_MAX;
	int i, shown = 0;
	if (!Friends_RequestVersion) Friends_FetchRequests();
	if (!Friends_RequestVersion)
		LLabel_Add(s, &s->lblStatus, "Loading...", fr_lblStatus);
	else
		LLabel_Add(s, &s->lblStatus, "", fr_lblStatus);
	for (i = start; i < Friends_RequestCount && shown < FREQ_MAX; i++, shown++) {
		LButton_Add(s, &s->entries[shown], 300, 30,
					Friends_Requests[i].nameBuf,
					FriendRequestsScreen_EntryClick, fr_entries[shown]);
	}
	if (Friends_RequestVersion == 0)
		LLabel_SetConst(&s->lblStatus, "Loading...");
	else if (Friends_RequestCount == 0)
		LLabel_SetConst(&s->lblStatus, "No pending requests!");
	else
		LLabel_SetConst(&s->lblStatus, "");
	LButton_Add(s, &s->btnBack, 200, 35, "Back", SwitchToFriends, fr_btnBack);
}

static void FriendRequestsScreen_Tick(struct LScreen* s_) {
	struct FriendRequestsScreen* s = (struct FriendRequestsScreen*)s_;
	if (s->lastVersion == Friends_RequestVersion) return;
	s->lastVersion = Friends_RequestVersion;
	if (Friends_RequestVersion) FriendRequestsScreen_SetActive();
}

void FriendRequestsScreen_SetActive(void) {
	struct FriendRequestsScreen* s = &FriendRequestsScreen;
	LScreen_Reset((struct LScreen*)s);
	s->widgets    = friendReq_widgets;
	s->maxWidgets = Array_Elems(friendReq_widgets);
	s->Activated  = FriendRequestsScreen_Activated;
	s->Tick       = FriendRequestsScreen_Tick;
	s->title      = "Friend Requests";
	s->lastVersion = Friends_RequestVersion;
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;
	Launcher_SetScreen((struct LScreen*)s);
}


/*########################################################################################################################*
*------------------------------------------FriendRequestManageScreen------------------------------------------------------*
*#########################################################################################################################*/
static struct FriendRequestManageScreen {
	LScreen_Layout
	struct LButton btnAccept, btnDecline, btnBack;
	struct LLabel  lblName;
	char   targetBuf[64];
	cc_string target;
} FriendRequestManageScreen CC_BIG_VAR;

static struct LWidget* friendReqManage_widgets[4];
LAYOUTS frm_lblName[]    = { { ANCHOR_CENTRE, 0 }, { ANCHOR_CENTRE, -60 } };
LAYOUTS frm_btnAccept[]  = { { ANCHOR_CENTRE, -80 }, { ANCHOR_CENTRE, 0 } };
LAYOUTS frm_btnDecline[] = { { ANCHOR_CENTRE,  80 }, { ANCHOR_CENTRE, 0 } };
LAYOUTS frm_btnBack[]    = { { ANCHOR_CENTRE,   0 }, { ANCHOR_MAX,   30 } };

static void FriendRequestManageScreen_Accept(void* w) {
	Friends_AcceptRequest(&FriendRequestManageScreen.target);
	FriendRequestsScreen_SetActive();
}
static void FriendRequestManageScreen_Decline(void* w) {
	Friends_DeclineRequest(&FriendRequestManageScreen.target);
	FriendRequestsScreen_SetActive();
}

static void FriendRequestManageScreen_Activated(struct LScreen* s_) {
	struct FriendRequestManageScreen* s = (struct FriendRequestManageScreen*)s_;
	char lbl[256]; cc_string lblStr;
	String_InitArray(lblStr, lbl);
	String_Format1(&lblStr, Lang_Get("Friend request from %s"), &s->target);
	LLabel_Add(s,  &s->lblName,    lbl,          frm_lblName);
	LButton_Add(s, &s->btnAccept,  140, 35, "Accept",
				FriendRequestManageScreen_Accept,  frm_btnAccept);
	LButton_Add(s, &s->btnDecline, 140, 35, "Decline",
				FriendRequestManageScreen_Decline, frm_btnDecline);
	LButton_Add(s, &s->btnBack,    200, 35, "Back",
				SwitchToFriendRequestsScreen, frm_btnBack);
}

void FriendRequestManageScreen_SetActive(const cc_string* from) {
	struct FriendRequestManageScreen* s = &FriendRequestManageScreen;
	LScreen_Reset((struct LScreen*)s);
	s->widgets    = friendReqManage_widgets;
	s->maxWidgets = Array_Elems(friendReqManage_widgets);
	s->Activated  = FriendRequestManageScreen_Activated;
	s->title      = "Friend Request";
	Mem_Copy(s->targetBuf, from->buffer,
			 from->length < 63 ? from->length : 63);
	s->targetBuf[from->length < 63 ? from->length : 63] = '\0';
	s->target = String_FromReadonly(s->targetBuf);
	s->onEscapeWidget = (struct LWidget*)&s->btnBack;
	Launcher_SetScreen((struct LScreen*)s);
}

/* end of friends screens */

#endif
