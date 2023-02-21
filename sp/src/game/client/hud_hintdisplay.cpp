//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================

#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "iclientmode.h"
#include "vgui_controls/AnimationController.h"
#include "vgui_controls/Label.h"
#include "vgui/ILocalize.h"
#include "vgui/ISurface.h"
#include "text_message.h"
#include "c_baseplayer.h"
#include "IGameUIFuncs.h"
#include "inputsystem/iinputsystem.h"
#ifdef STEAM_INPUT
#include "expanded_steam/isteaminput.h"
#include "vgui_controls/Image.h"
//#include "vgui_controls/HTML.h"
#include "filesystem.h"
#include "icommandline.h"
#include "img_png_loader.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: Displays hints across the center of the screen
//-----------------------------------------------------------------------------
class CHudHintDisplay : public vgui::Panel, public CHudElement
{
	DECLARE_CLASS_SIMPLE( CHudHintDisplay, vgui::Panel );

public:
	CHudHintDisplay( const char *pElementName );

	void Init();
	void Reset();
	void MsgFunc_HintText( bf_read &msg );
	void FireGameEvent( IGameEvent * event);

	bool SetHintText( wchar_t *text );
	void LocalizeAndDisplay( const char *pszHudTxtMsg, const char *szRawString );

	virtual void PerformLayout();

protected:
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );
	virtual void OnThink();

protected:
	vgui::HFont m_hFont;
	Color		m_bgColor;
	vgui::Label *m_pLabel;
	CUtlVector<vgui::Label *> m_Labels;
	CPanelAnimationVarAliasType( int, m_iTextX, "text_xpos", "8", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iTextY, "text_ypos", "8", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iCenterX, "center_x", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iCenterY, "center_y", "0", "proportional_int" );

	bool		m_bLastLabelUpdateHack;
	CPanelAnimationVar( float, m_flLabelSizePercentage, "HintSize", "0" );
};

DECLARE_HUDELEMENT( CHudHintDisplay );
DECLARE_HUD_MESSAGE( CHudHintDisplay, HintText );

#define MAX_HINT_STRINGS 5


//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHudHintDisplay::CHudHintDisplay( const char *pElementName ) : BaseClass(NULL, "HudHintDisplay"), CHudElement( pElementName )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );
	SetVisible( false );
	m_pLabel = new vgui::Label( this, "HudHintDisplayLabel", "" );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudHintDisplay::Init()
{
	HOOK_HUD_MESSAGE( CHudHintDisplay, HintText );

	// listen for client side events
	ListenForGameEvent( "player_hintmessage" );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudHintDisplay::Reset()
{
	SetHintText( NULL );
	g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( "HintMessageHide" ); 
	m_bLastLabelUpdateHack = true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudHintDisplay::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );

	SetFgColor( GetSchemeColor("HintMessageFg", pScheme) );
	m_hFont = pScheme->GetFont( "HudHintText", true );
	m_pLabel->SetBgColor( GetSchemeColor("HintMessageBg", pScheme) );
	m_pLabel->SetPaintBackgroundType( 2 );
	m_pLabel->SetSize( 0, GetTall() );		// Start tiny, it'll grow.
}

//-----------------------------------------------------------------------------
// Purpose: Sets the hint text, replacing variables as necessary
//-----------------------------------------------------------------------------
bool CHudHintDisplay::SetHintText( wchar_t *text )
{
	if ( text == NULL || text[0] == L'\0' )
	{
		return false;
	}

	// clear the existing text
	for (int i = 0; i < m_Labels.Count(); i++)
	{
		m_Labels[i]->MarkForDeletion();
#ifdef STEAM_INPUT
		if (m_Labels[i]->GetImageCount() > 1)
			vgui::surface()->DestroyTextureID( m_Labels[i]->GetImageAtIndex(0)->GetID() );
#endif
	}
	m_Labels.RemoveAll();

	wchar_t *p = text;

	while ( p )
	{
		wchar_t *line = p;
		wchar_t *end = wcschr( p, L'\n' );
		int linelengthbytes = 0;
		if ( end )
		{
			//*end = 0;	//eek
			p = end+1;
			linelengthbytes = ( end - line ) * 2;
		}
		else
		{
			p = NULL;
		}		

		// replace any key references with bound keys
		wchar_t buf[512];
		UTIL_ReplaceKeyBindings( line, linelengthbytes, buf, sizeof( buf ) );

		// put it in a label
		vgui::Label *label = vgui::SETUP_PANEL(new vgui::Label(this, NULL, buf));
		label->SetFont( m_hFont );
		label->SetPaintBackgroundEnabled( false );
		label->SetPaintBorderEnabled( false );
		label->SizeToContents();
		label->SetContentAlignment( vgui::Label::a_west );
		label->SetFgColor( GetFgColor() );
		m_Labels.AddToTail( vgui::SETUP_PANEL(label) );
	}

	InvalidateLayout( true );

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Resizes the label
//-----------------------------------------------------------------------------
void CHudHintDisplay::PerformLayout()
{
	BaseClass::PerformLayout();
	int i;

	int wide, tall;
	GetSize( wide, tall );

	// find the widest line
	int iDesiredLabelWide = 0;
	for ( i=0; i < m_Labels.Count(); ++i )
	{
		iDesiredLabelWide = MAX( iDesiredLabelWide, m_Labels[i]->GetWide() );
	}

	// find the total height
	int fontTall = vgui::surface()->GetFontTall( m_hFont );
	int labelTall = fontTall * m_Labels.Count();

	iDesiredLabelWide += m_iTextX*2;
	labelTall += m_iTextY*2;

	// Now clamp it to our animation size
	iDesiredLabelWide = (iDesiredLabelWide * m_flLabelSizePercentage);

	int x, y;
	if ( m_iCenterX < 0 )
	{
		x = 0;
	}
	else if ( m_iCenterX > 0 )
	{
		x = wide - iDesiredLabelWide;
	}
	else
	{
		x = (wide - iDesiredLabelWide) / 2;
	}

	if ( m_iCenterY > 0 )
	{
		y = 0;
	}
	else if ( m_iCenterY < 0 )
	{
		y = tall - labelTall;
	}
	else
	{
		y = (tall - labelTall) / 2;
	}

	x = MAX(x,0);
	y = MAX(y,0);

	iDesiredLabelWide = MIN(iDesiredLabelWide,wide);
	m_pLabel->SetBounds( x, y, iDesiredLabelWide, labelTall );

	// now lay out the sub-labels
	for ( i=0; i<m_Labels.Count(); ++i )
	{
		int xOffset = (wide - m_Labels[i]->GetWide()) * 0.5;
		m_Labels[i]->SetPos( xOffset, y + m_iTextY + i*fontTall );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Updates the label color each frame
//-----------------------------------------------------------------------------
void CHudHintDisplay::OnThink()
{
	m_pLabel->SetFgColor(GetFgColor());
	for (int i = 0; i < m_Labels.Count(); i++)
	{
		m_Labels[i]->SetFgColor(GetFgColor());
	}

	// If our label size isn't at the extreme's, we're sliding open / closed
	// This is a hack to get around InvalideLayout() not getting called when
	// m_flLabelSizePercentage is changed via a HudAnimation.
	if ( ( m_flLabelSizePercentage != 0.0 && m_flLabelSizePercentage != 1.0 ) || m_bLastLabelUpdateHack )
	{
		m_bLastLabelUpdateHack = (m_flLabelSizePercentage != 0.0 && m_flLabelSizePercentage != 1.0);
		InvalidateLayout();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Activates the hint display
//-----------------------------------------------------------------------------
void CHudHintDisplay::MsgFunc_HintText( bf_read &msg )
{
	// Read the string(s)
	char szString[255];
	msg.ReadString( szString, sizeof(szString) );

	char *tmpStr = hudtextmessage->LookupString( szString, NULL );
	LocalizeAndDisplay( tmpStr, szString );
}

//-----------------------------------------------------------------------------
// Purpose: Activates the hint display upon receiving a hint
//-----------------------------------------------------------------------------
void CHudHintDisplay::FireGameEvent( IGameEvent * event)
{
	const char *hintmessage = event->GetString( "hintmessage" );
	char *tmpStr = hudtextmessage->LookupString( hintmessage, NULL );
	LocalizeAndDisplay( tmpStr, hintmessage );
}

extern ConVar sv_hudhint_sound;
ConVar cl_hudhint_sound( "cl_hudhint_sound", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "Disable hudhint sounds." );

//-----------------------------------------------------------------------------
// Purpose: Localize, display, and animate the hud element
//-----------------------------------------------------------------------------
void CHudHintDisplay::LocalizeAndDisplay( const char *pszHudTxtMsg, const char *szRawString )
{
	static wchar_t szBuf[128];
	wchar_t *pszBuf;

	// init buffers & pointers
	szBuf[0] = 0;
	pszBuf = szBuf;

	// try to localize
	if ( pszHudTxtMsg )
	{
		pszBuf = g_pVGuiLocalize->Find( pszHudTxtMsg );
	}
	else
	{
		pszBuf = g_pVGuiLocalize->Find( szRawString );
	}

	if ( !pszBuf )
	{
		// use plain ASCII string 
		g_pVGuiLocalize->ConvertANSIToUnicode( szRawString, szBuf, sizeof(szBuf) );
		pszBuf = szBuf;
	}

	// make it visible
	if ( SetHintText( pszBuf ) )
	{
		SetVisible( true );
		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( "HintMessageShow" ); 

		C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();
		if ( pLocalPlayer )
		{
#ifndef HL2MP
			if ( sv_hudhint_sound.GetBool() && cl_hudhint_sound.GetBool() )
			{
				pLocalPlayer->EmitSound( "Hud.Hint" );
			}
#endif // HL2MP

			if ( pLocalPlayer->Hints() )
			{
				pLocalPlayer->Hints()->PlayedAHint();
			}
		}
	}
	else
	{
		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( "HintMessageHide" ); 
	}
}




//-----------------------------------------------------------------------------
// Purpose: Displays small key-centric hints on the right hand side of the screen
//-----------------------------------------------------------------------------
class CHudHintKeyDisplay : public vgui::Panel, public CHudElement
{
	DECLARE_CLASS_SIMPLE( CHudHintKeyDisplay, vgui::Panel );

public:
	CHudHintKeyDisplay( const char *pElementName );
	void Init();
	void Reset();
	void MsgFunc_KeyHintText( bf_read &msg );
	bool ShouldDraw();

	bool SetHintText( const char *text );

protected:
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );
	virtual void Paint();
	virtual void OnThink();

private:
	CUtlVector<vgui::Label *> m_Labels;
	vgui::HFont m_hSmallFont, m_hLargeFont;
	int		m_iBaseY;

#ifdef STEAM_INPUT
	bool	m_bDontTintButtons;

	CUtlVector<int> m_ButtonIcons;
	int m_iButtonSize = 32;

	union BindingChoice_t
	{
		const char *psz;
		int i;
	};
	
	// From CLocatorTarget in hud_locator_target.h
	int			m_bScrollingBinds;
	int			m_iBindingTick;
	float		m_flNextBindingTick;
	CUtlVector<CUtlVector<BindingChoice_t>> (m_pchBindingChoices);
	bool		m_bBindingChoicesWereAllocated = false;
#endif

	CPanelAnimationVarAliasType( float, m_iTextX, "text_xpos", "8", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_iTextY, "text_ypos", "8", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_iTextGapX, "text_xgap", "8", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_iTextGapY, "text_ygap", "8", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_iYOffset, "YOffset", "0", "proportional_float" );
};

DECLARE_HUDELEMENT( CHudHintKeyDisplay );
DECLARE_HUD_MESSAGE( CHudHintKeyDisplay, KeyHintText );

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHudHintKeyDisplay::CHudHintKeyDisplay( const char *pElementName ) : BaseClass(NULL, "HudHintKeyDisplay"), CHudElement( pElementName )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );
	SetVisible( false );
	SetAlpha( 0 );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudHintKeyDisplay::Init()
{
	HOOK_HUD_MESSAGE( CHudHintKeyDisplay, KeyHintText );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudHintKeyDisplay::Reset()
{
	SetHintText( NULL );
	SetAlpha( 0 );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudHintKeyDisplay::ApplySchemeSettings( vgui::IScheme *pScheme )
{
#ifdef STEAM_INPUT
	// Overridden since mods can't add conditionals to ClientScheme
	if (IsDeck())
	{
		m_hSmallFont = pScheme->GetFont( "HudHintTextSmall_Deck", true );
		m_hLargeFont = pScheme->GetFont( "HudHintTextLarge_Deck", true );
	}
	else
	{
		m_hSmallFont = pScheme->GetFont( "HudHintTextSmall", true );
		m_hLargeFont = pScheme->GetFont( "HudHintTextLarge", true );
	}
#else
	m_hSmallFont = pScheme->GetFont( "HudHintTextSmall", true );
	m_hLargeFont = pScheme->GetFont( "HudHintTextLarge", true );
#endif

	BaseClass::ApplySchemeSettings( pScheme );
}

//-----------------------------------------------------------------------------
// Purpose: Save CPU cycles by letting the HUD system early cull
// costly traversal.  Called per frame, return true if thinking and 
// painting need to occur.
//-----------------------------------------------------------------------------
bool CHudHintKeyDisplay::ShouldDraw( void )
{
	return ( ( GetAlpha() > 0 ) && CHudElement::ShouldDraw() );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudHintKeyDisplay::Paint()
{
	BaseClass::Paint();

#ifdef STEAM_INPUT
	if (m_ButtonIcons.Count() > 0)
	{
		for (int i = 0; i < m_Labels.Count(); i++)
		{
			if ((i & 1) == 0)
			{
				vgui::surface()->DrawSetColor( m_Labels[i]->GetFgColor() );

				int i2 = i / 2;
				vgui::surface()->DrawSetTexture( m_pchBindingChoices[i2][ m_iBindingTick % m_pchBindingChoices[i2].Count() ].i );

				int x, y;
				m_Labels[i]->GetPos( x, y );

				vgui::surface()->DrawTexturedRect( x, y, x + m_iButtonSize, y + m_iButtonSize );
			}
		}

		vgui::surface()->DrawSetTexture( 0 );
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Updates the label color each frame
//-----------------------------------------------------------------------------
void CHudHintKeyDisplay::OnThink()
{
#ifdef STEAM_INPUT
	if ( m_bScrollingBinds && gpGlobals->curtime >= m_flNextBindingTick )
	{
		m_iBindingTick++;
		m_flNextBindingTick = gpGlobals->curtime + 0.75f;
	}
#endif

	for (int i = 0; i < m_Labels.Count(); i++)
	{
#ifdef STEAM_INPUT
		if ( m_bDontTintButtons && ( i & 1 ) == 0 )
#else
		if ( IsX360() && ( i & 1 ) == 0 )
#endif
		{
			// Don't change the fg color for buttons (even numbered labels)
			m_Labels[i]->SetAlpha( GetFgColor().a() );
		}
		else
		{
			m_Labels[i]->SetFgColor(GetFgColor());
		}

#ifdef STEAM_INPUT
		if ((i & 1) == 0)
		{
			if (m_bScrollingBinds && m_pchBindingChoices[i / 2].Count() > 0)
			{
				// Cycle through the list of binds at a rate of 2 per second
				if (m_ButtonIcons.Count() == 0)
				{
					const char *pchBinding = m_pchBindingChoices[i/2][ m_iBindingTick % m_pchBindingChoices[i/2].Count() ].psz;
					if (pchBinding)
					{
						m_Labels[i]->SetText( pchBinding );
					}
				}
			}
		}
#endif
	}

	int ox, oy;
	GetPos(ox, oy);
	SetPos( ox, m_iBaseY + m_iYOffset );
}

//-----------------------------------------------------------------------------
// Purpose: Sets the hint text, replacing variables as necessary
//-----------------------------------------------------------------------------
bool CHudHintKeyDisplay::SetHintText( const char *text )
{
	if ( text == NULL || text[0] == L'\0' )
		return false;

	// clear the existing text
	for (int i = 0; i < m_Labels.Count(); i++)
	{
		m_Labels[i]->MarkForDeletion();
	}
	m_Labels.RemoveAll();

#ifdef STEAM_INPUT
	if (g_pSteamInput->IsEnabled())
	{
		// Remap the hint if needed
		g_pSteamInput->RemapHudHint( &text );
		m_flNextBindingTick = gpGlobals->curtime + 0.75f;
		m_iBindingTick = 0;
	}

	if (m_ButtonIcons.Count() > 0)
	{
		for (int i = m_ButtonIcons.Count()-1; i >= 0; i--)
		{
			vgui::surface()->DestroyTextureID( m_ButtonIcons[i] );
			m_ButtonIcons.FastRemove( i );
		}

		for (int i = m_pchBindingChoices.Count()-1; i >= 0; i--)
		{
			m_pchBindingChoices[i].RemoveAll();
			m_pchBindingChoices.FastRemove( i );
		}
	}
	else
	{
		for (int i = m_pchBindingChoices.Count()-1; i >= 0; i--)
		{
			if (m_bBindingChoicesWereAllocated)
			{
				for (int i2 = m_pchBindingChoices[i].Count()-1; i2 >= 0; i2--)
				{
					delete m_pchBindingChoices[i][i2].psz;
					m_pchBindingChoices[i].FastRemove( i2 );
				}
			}

			m_pchBindingChoices[i].Purge();
			m_pchBindingChoices.FastRemove( i );
		}
	}
	m_bScrollingBinds = false;
	m_bBindingChoicesWereAllocated = false;
	m_bDontTintButtons = false;
#endif

	// look up the text string
	wchar_t *ws = g_pVGuiLocalize->Find( text );

	wchar_t wszBuf[256];
	if ( !ws || wcslen(ws) <= 0)
	{
		if (text[0] == '#')
		{
			// We don't want to display a localization placeholder, do we?
			return false;
		}
		// use plain ASCII string 
		g_pVGuiLocalize->ConvertANSIToUnicode(text, wszBuf, sizeof(wszBuf));
		ws = wszBuf;
	}

	// parse out the text into a label set
	while ( *ws )
	{
		wchar_t token[256];
		bool isVar = false;

		// check for variables
		if ( *ws == '%' )
		{
			isVar = true;
			++ws;
		}

		// parse out the string
		wchar_t *end = wcschr( ws, '%' );
		if ( end )
		{
			wcsncpy( token, ws, MIN( end - ws, ARRAYSIZE(token)) );
			token[end - ws] = L'\0';	// force null termination
		}
		else
		{
			wcsncpy( token, ws, ARRAYSIZE(token) );
			token[ ARRAYSIZE(token) - 1 ] = L'\0';	// force null termination
		}

		ws += wcslen( token );
		if ( isVar )
		{
			// move over the end of the variable
			++ws; 
		}

		// put it in a label
		vgui::Label *label = vgui::SETUP_PANEL(new vgui::Label(this, NULL, token));

		bool bIsBitmap = false;

		// modify the label if necessary
		if ( isVar )
		{
			label->SetFont( m_hLargeFont );

			// lookup key names
			char binding[64];
			g_pVGuiLocalize->ConvertUnicodeToANSI( token, binding, sizeof(binding) );

			//!! change some key names into better names
			char friendlyName[64];

#if STEAM_INPUT
			if (g_pSteamInput->IsEnabled())
			{
				int labelIdx = 0;
				if (g_pSteamInput->IsEnabled())
				{
					labelIdx = m_pchBindingChoices.AddToTail();
				}

				if (g_pSteamInput->UseGlyphs())
				{
					label->SetFont( m_hLargeFont );
					label->SetText( "" );

					m_bDontTintButtons = true; //!g_pSteamInput->TintGlyphs();
					bIsBitmap = m_bDontTintButtons; // Prevents tinting

					float fontTall = vgui::surface()->GetFontTall( m_hLargeFont );
					m_iButtonSize = vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), Lerp( 0.5f, fontTall, 20.0f ) ); // TODO: More manageable value?

					int iRealSize = m_iButtonSize;
					CUtlVector <const char *> szStringList;
					CUtlVector <int> iButtonIcons;

					if (strchr(binding, '&'))
					{
						// '&' is a Mapbase feature which allows combinations of buttons to be shown in hints ("%walk&use%" >> "ALT + E")
						// However, that's not as feasible with controller buttons since they aren't , so for now we just scroll through them
						// even though that functionality is shared with buttons that have the same actions.
						char *token = strtok(binding, "&");
						while (token)
						{
							g_pSteamInput->GetGlyphPNGsForCommand( szStringList, token, iRealSize );
							token = strtok(NULL, "&");
						}
					}
					else
					{
						g_pSteamInput->GetGlyphPNGsForCommand( szStringList, binding, iRealSize );
					}

					for (int i = 0; i < szStringList.Count(); i++)
					{
						iButtonIcons.AddToTail( vgui::surface()->CreateNewTextureID( true ) );

						CUtlMemory< byte > image;
						int w, h;
						if ( !PNGtoRGBA( g_pFullFileSystem, szStringList[i], image, w, h ) )
						{
							Warning( "Can't find PNG buffer for %s\n", szStringList[i] );
						}
						else
						{
							vgui::surface()->DrawSetTextureRGBA( iButtonIcons[i], image.Base(), iRealSize, iRealSize, true, false );
						}

						int i2 = m_pchBindingChoices[labelIdx].AddToTail();
						m_pchBindingChoices[labelIdx][i2].i = iButtonIcons[i];

						//Msg( "Hint PNG: \"%s\" (%i)\n", szStringList[i], iButtonIcons[i] );
					}

					if ( szStringList.Count() > 1)
					{
						m_bScrollingBinds = true;
					}
					else if (szStringList.Count() == 0)
					{
						Warning( "No button PNGs found for \"%s\"\n", binding );
					}

					m_ButtonIcons.AddVectorToTail( iButtonIcons );
					
					bIsBitmap = true; // Prevents tinting
					m_bDontTintButtons = true;
				}
				else
				{
					// Fall back to Steam's localized strings
					const char *key = NULL;
					CUtlVector <const char*> szStringList;
#ifdef MAPBASE
					if (strchr(binding, '&'))
					{
						// "%walk&use%" >> "ALT + E"
						char *token = strtok(binding, "&");
						while (token)
						{
							key = VarArgs( "%s%s", key ? key : "", key ? " + " : "" );

							g_pSteamInput->GetButtonStringsForCommand( token, szStringList );

							for (int i = 0; i < szStringList.Count(); i++)
							{
								key = VarArgs( "%s%s%s", key ? key : "", key ? " / " : "", szStringList[i] );
							}

							token = strtok(NULL, "&");
						}
					}
					else if (binding[0] == '$')
					{
						// "%$COOL STRING DUDE%" >> "COOL STRING DUDE"
						key = binding + 1;
					}
					else
#endif
					{
						g_pSteamInput->GetButtonStringsForCommand( binding, szStringList );

						if ( szStringList.Count() > 1 )
						{
							m_bScrollingBinds = true;
							
							for (int i = 0; i < szStringList.Count(); i++)
							{
								int i2 = m_pchBindingChoices[labelIdx].AddToTail();
								m_pchBindingChoices[labelIdx][i2].psz = szStringList[i];
							}
						}

						if (szStringList.Count() > 0)
							key = szStringList[0];

#if 0
						for (int i = 0; i < szStringList.Count(); i++)
						{
							key = VarArgs( "%s%s%s", key ? key : "", key ? " / " : "", szStringList[i] );
						}
#endif
					}

					label->SetText( key );
				}
			}
			else
#endif
			if ( IsX360() )
			{
				int iNumBinds = 0;

				char szBuff[ 512 ];
				wchar_t szWideBuff[ 64 ];

				for ( int iCode = 0; iCode < BUTTON_CODE_LAST; ++iCode )
				{
					ButtonCode_t code = static_cast<ButtonCode_t>( iCode );

					bool bUseThisKey = false;

					// Only check against bind name if we haven't already forced this binding to be used
					const char *pBinding = gameuifuncs->GetBindingForButtonCode( code );

					if ( !pBinding )
						continue;

					bUseThisKey = ( Q_stricmp( pBinding, binding ) == 0 );

					if ( !bUseThisKey && 
						( Q_stricmp( pBinding, "+duck" ) == 0 || Q_stricmp( pBinding, "toggle_duck" ) == 0 ) && 
						( Q_stricmp( binding, "+duck" ) == 0 || Q_stricmp( binding, "toggle_duck" ) == 0 ) )
					{
						// +duck and toggle_duck are interchangable
						bUseThisKey = true;
					}

					if ( !bUseThisKey && 
						( Q_stricmp( pBinding, "+zoom" ) == 0 || Q_stricmp( pBinding, "toggle_zoom" ) == 0 ) && 
						( Q_stricmp( binding, "+zoom" ) == 0 || Q_stricmp( binding, "toggle_zoom" ) == 0 ) )
					{
						// +zoom and toggle_zoom are interchangable
						bUseThisKey = true;
					}

					// Don't use this bind in out list
					if ( !bUseThisKey )
						continue;

					// Turn localized string into icon character
					Q_snprintf( szBuff, sizeof( szBuff ), "#GameUI_Icons_%s", g_pInputSystem->ButtonCodeToString( static_cast<ButtonCode_t>( iCode ) ) );
					g_pVGuiLocalize->ConstructString( szWideBuff, sizeof( szWideBuff ), g_pVGuiLocalize->Find( szBuff ), 0 );
					g_pVGuiLocalize->ConvertUnicodeToANSI( szWideBuff, szBuff, sizeof( szBuff ) );

					// Add this icon to our list of keys to display
					friendlyName[ iNumBinds ] = szBuff[ 0 ];
					++iNumBinds;
				}

				friendlyName[ iNumBinds ] = '\0';

				if ( iNumBinds == 0 )
				{
					friendlyName[ 0 ] = '\0';
					label->SetFont( m_hSmallFont );
					label->SetText( "#GameUI_Icons_NONE" );
				}
				else
				{
					// 360 always uses bitmaps
					bIsBitmap = true;
					label->SetText( friendlyName );
				}
			}
			else
			{
				const char *key = engine->Key_LookupBinding( *binding == '+' ? binding + 1 : binding );
#ifdef MAPBASE
				if ( !key )
				{
					const char *pszNotBound = VarArgs("< %s, not bound >", *binding == '+' ? binding + 1 : binding);
					if (strchr(binding, '&'))
					{
						// "%walk&use%" >> "ALT + E"
						char *token = strtok(binding, "&");
						while (token)
						{
							const char *tokenkey = engine->Key_LookupBinding( *token == '+' ? token + 1 : token );

							key = VarArgs("%s%s%s", key ? key : "", key ? " + " : "", tokenkey ? tokenkey : pszNotBound);

							token = strtok(NULL, "&");
						}
					}
					else if (binding[0] == '$')
					{
						// "%$COOL STRING DUDE%" >> "COOL STRING DUDE"
						key = binding + 1;
					}
					else
					{
						key = pszNotBound;
					}
				}
#else
				if ( !key )
				{
					key = "< not bound >";
				}
#endif

				Q_snprintf( friendlyName, sizeof(friendlyName), "#%s", key );
				Q_strupr( friendlyName );

				// set the variable text - key may need to be localized (button images for example)
				wchar_t *locName = g_pVGuiLocalize->Find( friendlyName );
				if ( !locName || wcslen(locName) <= 0)
				{
					label->SetText( friendlyName + 1 );
				}
				else
				{
					// Assuming localized vars must be using a bitmap image. *May* not be the case, but since
					// keyboard bindings have never been localized in the past, they probably won't in the future either.
					bIsBitmap = true;
					label->SetText( locName );
				}
			}
		}
		else
		{
			label->SetFont( m_hSmallFont );
		}

		label->SetPaintBackgroundEnabled( false );
		label->SetPaintBorderEnabled( false );
		label->SizeToContents();
		label->SetContentAlignment( vgui::Label::a_west );
		if ( bIsBitmap && isVar )
		{
			// Don't change the color of the button art
			label->SetFgColor( Color(255,255,255,255) );
		}
		else
		{
			label->SetFgColor( GetFgColor() );
		}
		m_Labels.AddToTail( vgui::SETUP_PANEL(label) );
	}

// Enable this small block of code to test formatting and layout of hint messages
// with varying numbers of lines
#define TEST_KEYHINT_DISPLAY 0
#if TEST_KEYHINT_DISPLAY

	// clear the existing text
	for (int i = 0; i < m_Labels.Count(); i++)
	{
		m_Labels[i]->MarkForDeletion();
	}
	m_Labels.RemoveAll();

	const char* sampleText[] = 
	{
		"This is a test",
		"of the hint system\nwith a multi-line hint",
		"that\ngoes\non\nfor",
		"several",
		"lines"
	};

	for ( int i = 0; i < ARRAYSIZE(sampleText); ++i)
	{
		// put it in a label
		vgui::Label *label = vgui::SETUP_PANEL(new vgui::Label(this, NULL, sampleText[i]));

		label->SetFont( m_hSmallFont );
		label->SetPaintBackgroundEnabled( false );
		label->SetPaintBorderEnabled( false );
		label->SizeToContents();
		label->SetContentAlignment( vgui::Label::a_west );
		label->SetFgColor( GetFgColor() );
		m_Labels.AddToTail( vgui::SETUP_PANEL(label) );
	}
#endif

	// find the bounds we need to show
	int widest1 = 0, widest2 = 0;
	for (int i = 0; i < m_Labels.Count(); i++)
	{
		vgui::Label *label = m_Labels[i];

		if (i & 1)
		{
			// help text
			if (label->GetWide() > widest2)
			{
				widest2 = label->GetWide();
			}
		}
		else
		{
#ifdef STEAM_INPUT
			if (m_ButtonIcons.Count() > 0)
			{
				label->SetSize( m_iButtonSize, m_iButtonSize );
			}
#endif

			// variable
			if (label->GetWide() > widest1)
			{
				widest1 = label->GetWide();
			}
		}
	}

	// position the labels
	int col1_x = m_iTextX;
	int col2_x = m_iTextX + widest1 + m_iTextGapX;
	int col_y = m_iTextY;

	for (int i = 0; i < m_Labels.Count(); i += 2)
	{
		int rowHeight = 0;
		vgui::Label *label0 = m_Labels[i];
		int tall0 = label0->GetTall();
		rowHeight = tall0;

		if (i + 1 < m_Labels.Count())
		{
			vgui::Label *label1 = m_Labels[i + 1];
			int tall1 = label1->GetTall();
			rowHeight = MAX(tall0, tall1);
			label1->SetPos( col2_x, col_y + (rowHeight - tall1) / 2 );
		}

		label0->SetPos( col1_x, col_y + (rowHeight - tall0) / 2 );

		col_y += rowHeight + m_iTextGapY;
	}

	// move ourselves relative to our start position
	int newWide = m_iTextX + col2_x + widest2;
	int newTall = col_y;
	int ox, oy;
	GetPos(ox, oy);

	if (IsRightAligned())
	{
		int oldWide = GetWide();
		int diff = newWide - oldWide;
		ox -= diff;
	}

	if (IsBottomAligned())
	{
		int oldTall = GetTall();
		int diff = newTall - oldTall;
		oy -= diff;
	}

	// set the size of the hint panel to fit
 	SetPos( ox, oy );
 	SetSize( newWide, newTall );

	m_iBaseY = oy;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Activates the hint display
//-----------------------------------------------------------------------------
void CHudHintKeyDisplay::MsgFunc_KeyHintText( bf_read &msg )
{
	// how many strings do we receive ?
	int count = msg.ReadByte();

	// here we expect only one string
	if ( count != 1 )
	{
		DevMsg("CHudHintKeyDisplay::MsgFunc_KeyHintText: string count != 1.\n");
		return;
	}

	// read the string
	char szString[2048];
	msg.ReadString( szString, sizeof(szString) );

	// make it visible
	if ( SetHintText( szString ) )
	{
		SetVisible( true );
 		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( "KeyHintMessageShow" ); 
	}
	else
	{
		// it's being cleared, hide the panel
		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( "KeyHintMessageHide" ); 
	}
}
