/* This file is part of the KDE libraries
   Copyright (C) 2007, 2008 Matthew Woehlke <mw_triad@users.sourceforge.net>
   Copyright (C) 2003 Christoph Cullmann <cullmann@kde.org>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "kateconfig.h"

#include "kateglobal.h"
#include "katedefaultcolors.h"
#include "katerenderer.h"
#include "kateview.h"
#include "katedocument.h"
#include "kateschema.h"
#include "katepartdebug.h"

#include <math.h>

#include <KConfigGroup>
#include <KCharsets>

#include <QTextCodec>
#include <QStringListModel>
#include <QSettings>

//BEGIN KateConfig
KateConfig::KateConfig()
{
}

KateConfig::~KateConfig()
{
}

void KateConfig::configStart()
{
    configSessionNumber++;

    if (configSessionNumber > 1) {
        return;
    }

    configIsRunning = true;
}

void KateConfig::configEnd()
{
    if (configSessionNumber == 0) {
        return;
    }

    configSessionNumber--;

    if (configSessionNumber > 0) {
        return;
    }

    configIsRunning = false;

    updateConfig();
}
//END

//BEGIN KateDocumentConfig
KateGlobalConfig *KateGlobalConfig::s_global = nullptr;
KateDocumentConfig *KateDocumentConfig::s_global = nullptr;
KateViewConfig *KateViewConfig::s_global = nullptr;
KateRendererConfig *KateRendererConfig::s_global = nullptr;

KateGlobalConfig::KateGlobalConfig()
{
    s_global = this;

    // init with defaults from config or really hardcoded ones
    KConfigGroup cg(KTextEditor::EditorPrivate::config(), "Editor");
    readConfig(cg);
}

KateGlobalConfig::~KateGlobalConfig()
{
}

namespace
{
const char KEY_PROBER_TYPE[] = "Encoding Prober Type";
const char KEY_FALLBACK_ENCODING[] = "Fallback Encoding";
}

void KateGlobalConfig::readConfig(const KConfigGroup &config)
{
    configStart();

    setProberType((KEncodingProber::ProberType)config.readEntry(KEY_PROBER_TYPE, (int)KEncodingProber::Universal));
    setFallbackEncoding(config.readEntry(KEY_FALLBACK_ENCODING, ""));

    configEnd();
}

void KateGlobalConfig::writeConfig(KConfigGroup &config)
{
    config.writeEntry(KEY_PROBER_TYPE, (int)proberType());
    config.writeEntry(KEY_FALLBACK_ENCODING, fallbackEncoding());
}

void KateGlobalConfig::updateConfig()
{
    // write config
    KConfigGroup cg(KTextEditor::EditorPrivate::config(), "Editor");
    writeConfig(cg);
    KTextEditor::EditorPrivate::config()->sync();
}

void KateGlobalConfig::setProberType(KEncodingProber::ProberType proberType)
{
    configStart();
    m_proberType = proberType;
    configEnd();
}

const QString &KateGlobalConfig::fallbackEncoding() const
{
    return m_fallbackEncoding;
}

QTextCodec *KateGlobalConfig::fallbackCodec() const
{
    if (m_fallbackEncoding.isEmpty()) {
        return QTextCodec::codecForName("ISO 8859-15");
    }

    return KCharsets::charsets()->codecForName(m_fallbackEncoding);
}

bool KateGlobalConfig::setFallbackEncoding(const QString &encoding)
{
    QTextCodec *codec;
    bool found = false;
    if (encoding.isEmpty()) {
        codec = s_global->fallbackCodec();
        found = true;
    } else {
        codec = KCharsets::charsets()->codecForName(encoding, found);
    }

    if (!found || !codec) {
        return false;
    }

    configStart();
    m_fallbackEncoding = QString::fromLatin1(codec->name());
    configEnd();
    return true;
}

KateDocumentConfig::KateDocumentConfig()
    : m_tabWidthSet(false),
      m_indentationWidthSet(false),
      m_indentationModeSet(false),
      m_wordWrapSet(false),
      m_wordWrapAtSet(false),
      m_pageUpDownMovesCursorSet(false),
      m_keepExtraSpacesSet(false),
      m_indentPastedTextSet(false),
      m_backspaceIndentsSet(false),
      m_smartHomeSet(false),
      m_showTabsSet(false),
      m_showSpacesSet(false),

      m_replaceTabsDynSet(false),
      m_removeSpacesSet(false),
      m_newLineAtEofSet(false),
      m_overwiteModeSet(false),
      m_tabIndentsSet(false),
      m_encodingSet(false),
      m_eolSet(false),
      m_bomSet(false),
      m_allowEolDetectionSet(false),
      m_backupFlagsSet(false),
      m_backupPrefixSet(false),
      m_backupSuffixSet(false),
      m_swapFileModeSet(false),
      m_swapDirectorySet(false),
      m_swapSyncIntervalSet(false),
      m_onTheFlySpellCheckSet(false),
      m_lineLengthLimitSet(false)

{
    s_global = this;

    // init with defaults from config or really hardcoded ones
    KConfigGroup cg(KTextEditor::EditorPrivate::config(), "Document");
    readConfig(cg);
}

KateDocumentConfig::KateDocumentConfig(const KConfigGroup &cg)
    : m_indentationWidth(2),
      m_tabWidth(4),
      m_tabHandling(tabSmart),
      m_configFlags(0),
      m_wordWrapAt(80),
      m_tabWidthSet(false),
      m_indentationWidthSet(false),
      m_indentationModeSet(false),
      m_wordWrapSet(false),
      m_wordWrapAtSet(false),
      m_pageUpDownMovesCursorSet(false),
      m_keepExtraSpacesSet(false),
      m_indentPastedTextSet(false),
      m_backspaceIndentsSet(false),
      m_smartHomeSet(false),
      m_showTabsSet(false),
      m_showSpacesSet(false),
      m_markerSize(1),
      m_replaceTabsDynSet(false),
      m_removeSpacesSet(false),
      m_newLineAtEofSet(false),
      m_overwiteModeSet(false),
      m_tabIndentsSet(false),
      m_encodingSet(false),
      m_eolSet(false),
      m_bomSet(false),
      m_allowEolDetectionSet(false),
      m_backupFlagsSet(false),
      m_backupPrefixSet(false),
      m_backupSuffixSet(false),
      m_swapFileModeSet(false),
      m_swapDirectorySet(false),
      m_swapSyncIntervalSet(false),
      m_onTheFlySpellCheckSet(false),
      m_lineLengthLimitSet(false),
      m_doc(nullptr)
{
    // init with defaults from config or really hardcoded ones
    readConfig(cg);
}

KateDocumentConfig::KateDocumentConfig(KTextEditor::DocumentPrivate *doc)
    : m_tabHandling(tabSmart),
      m_configFlags(0),
      m_tabWidthSet(false),
      m_indentationWidthSet(false),
      m_indentationModeSet(false),
      m_wordWrapSet(false),
      m_wordWrapAtSet(false),
      m_pageUpDownMovesCursorSet(false),
      m_keepExtraSpacesSet(false),
      m_indentPastedTextSet(false),
      m_backspaceIndentsSet(false),
      m_smartHomeSet(false),
      m_showTabsSet(false),
      m_showSpacesSet(false),
      m_markerSize(1),
      m_replaceTabsDynSet(false),
      m_removeSpacesSet(false),
      m_newLineAtEofSet(false),
      m_overwiteModeSet(false),
      m_tabIndentsSet(false),
      m_encodingSet(false),
      m_eolSet(false),
      m_bomSet(false),
      m_allowEolDetectionSet(false),
      m_backupFlagsSet(false),
      m_backupPrefixSet(false),
      m_backupSuffixSet(false),
      m_swapFileModeSet(false),
      m_swapDirectorySet(false),
      m_swapSyncIntervalSet(false),
      m_onTheFlySpellCheckSet(false),
      m_lineLengthLimitSet(false),
      m_doc(doc)
{
}

KateDocumentConfig::~KateDocumentConfig()
{
}

namespace
{
const char KEY_TAB_WIDTH[] = "Tab Width";
const char KEY_INDENTATION_WIDTH[] = "Indentation Width";
const char KEY_INDENTATION_MODE[] = "Indentation Mode";
const char KEY_TAB_HANDLING[] = "Tab Handling";
const char KEY_WORD_WRAP[] = "Word Wrap";
const char KEY_WORD_WRAP_AT[] = "Word Wrap Column";
const char KEY_PAGEUP_DOWN_MOVES_CURSOR[] = "PageUp/PageDown Moves Cursor";
const char KEY_SMART_HOME[] = "Smart Home";
const char KEY_SHOW_TABS[] = "Show Tabs";
const char KEY_TAB_INDENTS[] = "Indent On Tab";
const char KEY_KEEP_EXTRA_SPACES[] = "Keep Extra Spaces";
const char KEY_INDENT_PASTED_TEXT[] = "Indent On Text Paste";
const char KEY_BACKSPACE_INDENTS[] = "Indent On Backspace";
const char KEY_SHOW_SPACES[] = "Show Spaces";
const char KEY_MARKER_SIZE[] = "Trailing Marker Size";
const char KEY_REPLACE_TABS_DYN[] = "ReplaceTabsDyn";
const char KEY_REMOVE_SPACES[] = "Remove Spaces";
const char KEY_NEWLINE_AT_EOF[] = "Newline at End of File";
const char KEY_OVR[] = "Overwrite Mode";
const char KEY_ENCODING[] = "Encoding";
const char KEY_EOL[] = "End of Line";
const char KEY_ALLOW_EOL_DETECTION[] = "Allow End of Line Detection";
const char KEY_BOM[] = "BOM";
const char KEY_BACKUP_FLAGS[] = "Backup Flags";
const char KEY_BACKUP_PREFIX[] = "Backup Prefix";
const char KEY_BACKUP_SUFFIX[] = "Backup Suffix";
const char KEY_SWAP_FILE_MODE[] = "Swap File Mode";
const char KEY_SWAP_DIRECTORY[] = "Swap Directory";
const char KEY_SWAP_SYNC_INTERVAL[] = "Swap Sync Interval";
const char KEY_ON_THE_FLY_SPELLCHECK[] = "On-The-Fly Spellcheck";
const char KEY_LINE_LENGTH_LIMIT[] = "Line Length Limit";
}

void KateDocumentConfig::readConfig(const KConfigGroup &config)
{
    configStart();

    setTabWidth(config.readEntry(KEY_TAB_WIDTH, 4));

    setIndentationWidth(config.readEntry(KEY_INDENTATION_WIDTH, 4));

    setIndentationMode(config.readEntry(KEY_INDENTATION_MODE, "normal"));

    setTabHandling(config.readEntry(KEY_TAB_HANDLING, int(KateDocumentConfig::tabSmart)));

    setWordWrap(config.readEntry(KEY_WORD_WRAP, false));
    setWordWrapAt(config.readEntry(KEY_WORD_WRAP_AT, 80));
    setPageUpDownMovesCursor(config.readEntry(KEY_PAGEUP_DOWN_MOVES_CURSOR, false));

    setSmartHome(config.readEntry(KEY_SMART_HOME, true));
    setShowTabs(config.readEntry(KEY_SHOW_TABS, true));
    setTabIndents(config.readEntry(KEY_TAB_INDENTS, true));
    setKeepExtraSpaces(config.readEntry(KEY_KEEP_EXTRA_SPACES, false));
    setIndentPastedText(config.readEntry(KEY_INDENT_PASTED_TEXT, false));
    setBackspaceIndents(config.readEntry(KEY_BACKSPACE_INDENTS, true));
    setShowSpaces(config.readEntry(KEY_SHOW_SPACES, false));
    setMarkerSize(config.readEntry(KEY_MARKER_SIZE, 1));
    setReplaceTabsDyn(config.readEntry(KEY_REPLACE_TABS_DYN, true));
    setRemoveSpaces(config.readEntry(KEY_REMOVE_SPACES, 0));
    setNewLineAtEof(config.readEntry(KEY_NEWLINE_AT_EOF, true));
    setOvr(config.readEntry(KEY_OVR, false));

    setEncoding(config.readEntry(KEY_ENCODING, ""));

    setEol(config.readEntry(KEY_EOL, 0));
    setAllowEolDetection(config.readEntry(KEY_ALLOW_EOL_DETECTION, true));

    setBom(config.readEntry(KEY_BOM, false));

    setBackupFlags(config.readEntry(KEY_BACKUP_FLAGS, 0));

    setBackupPrefix(config.readEntry(KEY_BACKUP_PREFIX, QString()));

    setBackupSuffix(config.readEntry(KEY_BACKUP_SUFFIX, QStringLiteral("~")));

    setSwapFileMode(config.readEntry(KEY_SWAP_FILE_MODE, (uint)EnableSwapFile));
    setSwapDirectory(config.readEntry(KEY_SWAP_DIRECTORY, QString()));
    setSwapSyncInterval(config.readEntry(KEY_SWAP_SYNC_INTERVAL, 15));

    setOnTheFlySpellCheck(config.readEntry(KEY_ON_THE_FLY_SPELLCHECK, false));

    setLineLengthLimit(config.readEntry(KEY_LINE_LENGTH_LIMIT, 4096));

    configEnd();
}

void KateDocumentConfig::writeConfig(KConfigGroup &config)
{
    config.writeEntry(KEY_TAB_WIDTH, tabWidth());

    config.writeEntry(KEY_INDENTATION_WIDTH, indentationWidth());
    config.writeEntry(KEY_INDENTATION_MODE, indentationMode());

    config.writeEntry(KEY_TAB_HANDLING, tabHandling());

    config.writeEntry(KEY_WORD_WRAP, wordWrap());
    config.writeEntry(KEY_WORD_WRAP_AT, wordWrapAt());

    config.writeEntry(KEY_PAGEUP_DOWN_MOVES_CURSOR, pageUpDownMovesCursor());

    config.writeEntry(KEY_SMART_HOME, smartHome());
    config.writeEntry(KEY_SHOW_TABS, showTabs());
    config.writeEntry(KEY_TAB_INDENTS, tabIndentsEnabled());
    config.writeEntry(KEY_KEEP_EXTRA_SPACES, keepExtraSpaces());
    config.writeEntry(KEY_INDENT_PASTED_TEXT, indentPastedText());
    config.writeEntry(KEY_BACKSPACE_INDENTS, backspaceIndents());
    config.writeEntry(KEY_SHOW_SPACES, showSpaces());
    config.writeEntry(KEY_MARKER_SIZE, markerSize());
    config.writeEntry(KEY_REPLACE_TABS_DYN, replaceTabsDyn());
    config.writeEntry(KEY_REMOVE_SPACES, removeSpaces());
    config.writeEntry(KEY_NEWLINE_AT_EOF, newLineAtEof());
    config.writeEntry(KEY_OVR, ovr());

    config.writeEntry(KEY_ENCODING, encoding());

    config.writeEntry(KEY_EOL, eol());
    config.writeEntry(KEY_ALLOW_EOL_DETECTION, allowEolDetection());

    config.writeEntry(KEY_BOM, bom());

    config.writeEntry(KEY_BACKUP_FLAGS, backupFlags());

    config.writeEntry(KEY_BACKUP_PREFIX, backupPrefix());

    config.writeEntry(KEY_BACKUP_SUFFIX, backupSuffix());

    config.writeEntry(KEY_SWAP_FILE_MODE, swapFileModeRaw());
    config.writeEntry(KEY_SWAP_DIRECTORY, swapDirectory());
    config.writeEntry(KEY_SWAP_SYNC_INTERVAL, swapSyncInterval());

    config.writeEntry(KEY_ON_THE_FLY_SPELLCHECK, onTheFlySpellCheck());

    config.writeEntry(KEY_LINE_LENGTH_LIMIT, lineLengthLimit());
}

void KateDocumentConfig::updateConfig()
{
    if (m_doc) {
        m_doc->updateConfig();
        return;
    }

    if (isGlobal()) {
        for (int z = 0; z < KTextEditor::EditorPrivate::self()->kateDocuments().size(); ++z) {
            (KTextEditor::EditorPrivate::self()->kateDocuments())[z]->updateConfig();
        }

        // write config
        KConfigGroup cg(KTextEditor::EditorPrivate::config(), "Document");
        writeConfig(cg);
        KTextEditor::EditorPrivate::config()->sync();
    }
}

int KateDocumentConfig::tabWidth() const
{
    if (m_tabWidthSet || isGlobal()) {
        return m_tabWidth;
    }

    return s_global->tabWidth();
}

void KateDocumentConfig::setTabWidth(int tabWidth)
{
    if (tabWidth < 1) {
        return;
    }

    if (m_tabWidthSet && m_tabWidth == tabWidth) {
        return;
    }

    configStart();

    m_tabWidthSet = true;
    m_tabWidth = tabWidth;

    configEnd();
}

int KateDocumentConfig::indentationWidth() const
{
    if (m_indentationWidthSet || isGlobal()) {
        return m_indentationWidth;
    }

    return s_global->indentationWidth();
}

void KateDocumentConfig::setIndentationWidth(int indentationWidth)
{
    if (indentationWidth < 1) {
        return;
    }

    if (m_indentationWidthSet && m_indentationWidth == indentationWidth) {
        return;
    }

    configStart();

    m_indentationWidthSet = true;
    m_indentationWidth = indentationWidth;

    configEnd();
}

const QString &KateDocumentConfig::indentationMode() const
{
    if (m_indentationModeSet || isGlobal()) {
        return m_indentationMode;
    }

    return s_global->indentationMode();
}

void KateDocumentConfig::setIndentationMode(const QString &indentationMode)
{
    if (m_indentationModeSet && m_indentationMode == indentationMode) {
        return;
    }

    configStart();

    m_indentationModeSet = true;
    m_indentationMode = indentationMode;

    configEnd();
}

uint KateDocumentConfig::tabHandling() const
{
    // This setting is purly a user preference,
    // hence, there exists only the global setting.
    if (isGlobal()) {
        return m_tabHandling;
    }

    return s_global->tabHandling();
}

void KateDocumentConfig::setTabHandling(uint tabHandling)
{
    configStart();

    m_tabHandling = tabHandling;

    configEnd();
}

bool KateDocumentConfig::wordWrap() const
{
    if (m_wordWrapSet || isGlobal()) {
        return m_wordWrap;
    }

    return s_global->wordWrap();
}

void KateDocumentConfig::setWordWrap(bool on)
{
    if (m_wordWrapSet && m_wordWrap == on) {
        return;
    }

    configStart();

    m_wordWrapSet = true;
    m_wordWrap = on;

    configEnd();
}

int KateDocumentConfig::wordWrapAt() const
{
    if (m_wordWrapAtSet || isGlobal()) {
        return m_wordWrapAt;
    }

    return s_global->wordWrapAt();
}

void KateDocumentConfig::setWordWrapAt(int col)
{
    if (col < 1) {
        return;
    }

    if (m_wordWrapAtSet && m_wordWrapAt == col) {
        return;
    }

    configStart();

    m_wordWrapAtSet = true;
    m_wordWrapAt = col;

    configEnd();
}

bool KateDocumentConfig::pageUpDownMovesCursor() const
{
    if (m_pageUpDownMovesCursorSet || isGlobal()) {
        return m_pageUpDownMovesCursor;
    }

    return s_global->pageUpDownMovesCursor();
}

void KateDocumentConfig::setPageUpDownMovesCursor(bool on)
{
    if (m_pageUpDownMovesCursorSet && m_pageUpDownMovesCursor == on) {
        return;
    }

    configStart();

    m_pageUpDownMovesCursorSet = true;
    m_pageUpDownMovesCursor = on;

    configEnd();
}

void KateDocumentConfig::setKeepExtraSpaces(bool on)
{
    if (m_keepExtraSpacesSet && m_keepExtraSpaces == on) {
        return;
    }

    configStart();

    m_keepExtraSpacesSet = true;
    m_keepExtraSpaces = on;

    configEnd();
}

bool KateDocumentConfig::keepExtraSpaces() const
{
    if (m_keepExtraSpacesSet || isGlobal()) {
        return m_keepExtraSpaces;
    }

    return s_global->keepExtraSpaces();
}

void KateDocumentConfig::setIndentPastedText(bool on)
{
    if (m_indentPastedTextSet && m_indentPastedText == on) {
        return;
    }

    configStart();

    m_indentPastedTextSet = true;
    m_indentPastedText = on;

    configEnd();
}

bool KateDocumentConfig::indentPastedText() const
{
    if (m_indentPastedTextSet || isGlobal()) {
        return m_indentPastedText;
    }

    return s_global->indentPastedText();
}

void KateDocumentConfig::setBackspaceIndents(bool on)
{
    if (m_backspaceIndentsSet && m_backspaceIndents == on) {
        return;
    }

    configStart();

    m_backspaceIndentsSet = true;
    m_backspaceIndents = on;

    configEnd();
}

bool KateDocumentConfig::backspaceIndents() const
{
    if (m_backspaceIndentsSet || isGlobal()) {
        return m_backspaceIndents;
    }

    return s_global->backspaceIndents();
}

void KateDocumentConfig::setSmartHome(bool on)
{
    if (m_smartHomeSet && m_smartHome == on) {
        return;
    }

    configStart();

    m_smartHomeSet = true;
    m_smartHome = on;

    configEnd();
}

bool KateDocumentConfig::smartHome() const
{
    if (m_smartHomeSet || isGlobal()) {
        return m_smartHome;
    }

    return s_global->smartHome();
}

void KateDocumentConfig::setShowTabs(bool on)
{
    if (m_showTabsSet && m_showTabs == on) {
        return;
    }

    configStart();

    m_showTabsSet = true;
    m_showTabs = on;

    configEnd();
}

bool KateDocumentConfig::showTabs() const
{
    if (m_showTabsSet || isGlobal()) {
        return m_showTabs;
    }

    return s_global->showTabs();
}

void KateDocumentConfig::setShowSpaces(bool on)
{
    if (m_showSpacesSet && m_showSpaces == on) {
        return;
    }

    configStart();

    m_showSpacesSet = true;
    m_showSpaces = on;

    configEnd();
}

bool KateDocumentConfig::showSpaces() const
{
    if (m_showSpacesSet || isGlobal()) {
        return m_showSpaces;
    }

    return s_global->showSpaces();
}

void KateDocumentConfig::setMarkerSize(uint markerSize)
{
    if (m_markerSize == markerSize) {
        return;
    }

    configStart();

    m_markerSize = markerSize;

    configEnd();
}

uint KateDocumentConfig::markerSize() const
{
    if (isGlobal()) {
        return m_markerSize;
    }

    return s_global->markerSize();
}

void KateDocumentConfig::setReplaceTabsDyn(bool on)
{
    if (m_replaceTabsDynSet && m_replaceTabsDyn == on) {
        return;
    }

    configStart();

    m_replaceTabsDynSet = true;
    m_replaceTabsDyn = on;

    configEnd();
}

bool KateDocumentConfig::replaceTabsDyn() const
{
    if (m_replaceTabsDynSet || isGlobal()) {
        return m_replaceTabsDyn;
    }

    return s_global->replaceTabsDyn();
}

void KateDocumentConfig::setRemoveSpaces(int triState)
{
    if (m_removeSpacesSet && m_removeSpaces == triState) {
        return;
    }

    configStart();

    m_removeSpacesSet = true;
    m_removeSpaces = triState;

    configEnd();
}

int KateDocumentConfig::removeSpaces() const
{
    if (m_removeSpacesSet || isGlobal()) {
        return m_removeSpaces;
    }

    return s_global->removeSpaces();
}

void KateDocumentConfig::setNewLineAtEof(bool on)
{
    if (m_newLineAtEofSet && m_newLineAtEof == on) {
        return;
    }

    configStart();

    m_newLineAtEofSet = true;
    m_newLineAtEof = on;

    configEnd();
}

bool KateDocumentConfig::newLineAtEof() const
{
    if (m_newLineAtEofSet || isGlobal()) {
        return m_newLineAtEof;
    }

    return s_global->newLineAtEof();
}

void KateDocumentConfig::setOvr(bool on)
{
    if (m_overwiteModeSet && m_overwiteMode == on) {
        return;
    }

    configStart();

    m_overwiteModeSet = true;
    m_overwiteMode = on;

    configEnd();
}

bool KateDocumentConfig::ovr() const
{
    if (m_overwiteModeSet || isGlobal()) {
        return m_overwiteMode;
    }

    return s_global->ovr();
}

void KateDocumentConfig::setTabIndents(bool on)
{
    if (m_tabIndentsSet && m_tabIndents == on) {
        return;
    }

    configStart();

    m_tabIndentsSet = true;
    m_tabIndents = on;

    configEnd();
}

bool KateDocumentConfig::tabIndentsEnabled() const
{
    if (m_tabIndentsSet || isGlobal()) {
        return m_tabIndents;
    }

    return s_global->tabIndentsEnabled();
}

const QString &KateDocumentConfig::encoding() const
{
    if (m_encodingSet || isGlobal()) {
        return m_encoding;
    }

    return s_global->encoding();
}

QTextCodec *KateDocumentConfig::codec() const
{
    if (m_encodingSet || isGlobal()) {
        if (m_encoding.isEmpty() && isGlobal()) {
            // default to UTF-8, this makes sense to have a usable encoding detection
            // else for people that have by bad luck some encoding like latin1 as default, no encoding detection will work
            // see e.g. bug 362604 for windows
            return QTextCodec::codecForName("UTF-8");
        } else if (m_encoding.isEmpty()) {
            return s_global->codec();
        } else {
            return KCharsets::charsets()->codecForName(m_encoding);
        }
    }

    return s_global->codec();
}

bool KateDocumentConfig::setEncoding(const QString &encoding)
{
    QTextCodec *codec;
    bool found = false;
    if (encoding.isEmpty()) {
        codec = s_global->codec();
        found = true;
    } else {
        codec = KCharsets::charsets()->codecForName(encoding, found);
    }

    if (!found || !codec) {
        return false;
    }

    configStart();
    m_encodingSet = true;
    m_encoding = QString::fromLatin1(codec->name());
    configEnd();
    return true;
}

bool KateDocumentConfig::isSetEncoding() const
{
    return m_encodingSet;
}

int KateDocumentConfig::eol() const
{
    if (m_eolSet || isGlobal()) {
        return m_eol;
    }

    return s_global->eol();
}

QString KateDocumentConfig::eolString()
{
    if (eol() == KateDocumentConfig::eolUnix) {
        return QStringLiteral("\n");
    } else if (eol() == KateDocumentConfig::eolDos) {
        return QStringLiteral("\r\n");
    } else if (eol() == KateDocumentConfig::eolMac) {
        return QStringLiteral("\r");
    }

    return QStringLiteral("\n");
}

void KateDocumentConfig::setEol(int mode)
{
    if (m_eolSet && m_eol == mode) {
        return;
    }

    configStart();

    m_eolSet = true;
    m_eol = mode;

    configEnd();
}

void KateDocumentConfig::setBom(bool bom)
{
    if (m_bomSet && m_bom == bom) {
        return;
    }

    configStart();

    m_bomSet = true;
    m_bom = bom;

    configEnd();
}

bool KateDocumentConfig::bom() const
{
    if (m_bomSet || isGlobal()) {
        return m_bom;
    }

    return s_global->bom();
}

bool KateDocumentConfig::allowEolDetection() const
{
    if (m_allowEolDetectionSet || isGlobal()) {
        return m_allowEolDetection;
    }

    return s_global->allowEolDetection();
}

void KateDocumentConfig::setAllowEolDetection(bool on)
{
    if (m_allowEolDetectionSet && m_allowEolDetection == on) {
        return;
    }

    configStart();

    m_allowEolDetectionSet = true;
    m_allowEolDetection = on;

    configEnd();
}

uint KateDocumentConfig::backupFlags() const
{
    if (m_backupFlagsSet || isGlobal()) {
        return m_backupFlags;
    }

    return s_global->backupFlags();
}

void KateDocumentConfig::setBackupFlags(uint flags)
{
    if (m_backupFlagsSet && m_backupFlags == flags) {
        return;
    }

    configStart();

    m_backupFlagsSet = true;
    m_backupFlags = flags;

    configEnd();
}

const QString &KateDocumentConfig::backupPrefix() const
{
    if (m_backupPrefixSet || isGlobal()) {
        return m_backupPrefix;
    }

    return s_global->backupPrefix();
}

const QString &KateDocumentConfig::backupSuffix() const
{
    if (m_backupSuffixSet || isGlobal()) {
        return m_backupSuffix;
    }

    return s_global->backupSuffix();
}

void KateDocumentConfig::setBackupPrefix(const QString &prefix)
{
    if (m_backupPrefixSet && m_backupPrefix == prefix) {
        return;
    }

    configStart();

    m_backupPrefixSet = true;
    m_backupPrefix = prefix;

    configEnd();
}

void KateDocumentConfig::setBackupSuffix(const QString &suffix)
{
    if (m_backupSuffixSet && m_backupSuffix == suffix) {
        return;
    }

    configStart();

    m_backupSuffixSet = true;
    m_backupSuffix = suffix;

    configEnd();
}

uint KateDocumentConfig::swapSyncInterval() const
{
    if (m_swapSyncIntervalSet || isGlobal()) {
        return m_swapSyncInterval;
    }

    return s_global->swapSyncInterval();
}

void KateDocumentConfig::setSwapSyncInterval(uint interval)
{
    if (m_swapSyncIntervalSet && m_swapSyncInterval == interval) {
        return;
    }

    configStart();

    m_swapSyncIntervalSet = true;
    m_swapSyncInterval = interval;

    configEnd();
}

uint KateDocumentConfig::swapFileModeRaw() const
{
    if (m_swapFileModeSet || isGlobal()) {
        return m_swapFileMode;
    }

    return s_global->swapFileModeRaw();
}

KateDocumentConfig::SwapFileMode KateDocumentConfig::swapFileMode() const
{
    return static_cast<KateDocumentConfig::SwapFileMode>(swapFileModeRaw());
}

void KateDocumentConfig::setSwapFileMode(uint mode)
{
    if (m_swapFileModeSet && m_swapFileMode == mode) {
        return;
    }

    configStart();

    m_swapFileModeSet = true;
    m_swapFileMode = mode;

    configEnd();
}

const QString &KateDocumentConfig::swapDirectory() const
{
    if (m_swapDirectorySet || isGlobal()) {
        return m_swapDirectory;
    }

    return s_global->swapDirectory();
}

void KateDocumentConfig::setSwapDirectory(const QString &directory)
{
    if (m_swapDirectorySet && m_swapDirectory == directory) {
        return;
    }

    configStart();

    m_swapDirectorySet = true;
    m_swapDirectory = directory;

    configEnd();
}

bool KateDocumentConfig::onTheFlySpellCheck() const
{
    if (isGlobal()) {
        // WARNING: this is slightly hackish, but it's currently the only way to
        //          do it, see also the KTextEdit class
        QSettings settings(QStringLiteral("KDE"), QStringLiteral("Sonnet"));
        return settings.value(QStringLiteral("checkerEnabledByDefault"), false).toBool();
        //KConfigGroup configGroup(KSharedConfig::openConfig(), "Spelling");
        //return configGroup.readEntry("checkerEnabledByDefault", false);
    }
    if (m_onTheFlySpellCheckSet) {
        return m_onTheFlySpellCheck;
    }

    return s_global->onTheFlySpellCheck();
}

void KateDocumentConfig::setOnTheFlySpellCheck(bool on)
{
    if (m_onTheFlySpellCheckSet && m_onTheFlySpellCheck == on) {
        return;
    }

    configStart();

    m_onTheFlySpellCheckSet = true;
    m_onTheFlySpellCheck = on;

    configEnd();
}

int KateDocumentConfig::lineLengthLimit() const
{
    if (m_lineLengthLimitSet || isGlobal()) {
        return m_lineLengthLimit;
    }

    return s_global->lineLengthLimit();
}

void KateDocumentConfig::setLineLengthLimit(int lineLengthLimit)
{
    if (m_lineLengthLimitSet && m_lineLengthLimit == lineLengthLimit) {
        return;
    }

    configStart();

    m_lineLengthLimitSet = true;
    m_lineLengthLimit = lineLengthLimit;

    configEnd();
}

//END

//BEGIN KateViewConfig
KateViewConfig::KateViewConfig()
    :

    m_dynWordWrapSet(false),
    m_dynWordWrapIndicatorsSet(false),
    m_dynWordWrapAlignIndentSet(false),
    m_lineNumbersSet(false),
    m_scrollBarMarksSet(false),
    m_scrollBarPreviewSet(false),
    m_scrollBarMiniMapSet(false),
    m_scrollBarMiniMapAllSet(false),
    m_scrollBarMiniMapWidthSet(false),
    m_showScrollbarsSet(false),
    m_iconBarSet(false),
    m_foldingBarSet(false),
    m_foldingPreviewSet(false),
    m_lineModificationSet(false),
    m_bookmarkSortSet(false),
    m_autoCenterLinesSet(false),
    m_searchFlagsSet(false),
    m_defaultMarkTypeSet(false),
    m_persistentSelectionSet(false),
    m_inputModeSet(false),
    m_viInputModeStealKeysSet(false),
    m_viRelativeLineNumbersSet(false),
    m_automaticCompletionInvocationSet(false),
    m_wordCompletionSet(false),
    m_keywordCompletionSet(false),
    m_wordCompletionMinimalWordLengthSet(false),
    m_smartCopyCutSet(false),
    m_scrollPastEndSet(false),
    m_allowMarkMenu(true),
    m_wordCompletionRemoveTailSet(false),
    m_foldFirstLineSet (false),
    m_showWordCountSet(false),
    m_showLinesCountSet(false),
    m_autoBracketsSet(false),
    m_backspaceRemoveComposedSet(false)

{
    s_global = this;

    // init with defaults from config or really hardcoded ones
    KConfigGroup config(KTextEditor::EditorPrivate::config(), "View");
    readConfig(config);
}

KateViewConfig::KateViewConfig(KTextEditor::ViewPrivate *view)
    :
    m_searchFlags(PowerModePlainText),
    m_maxHistorySize(100),
    m_showWordCount(false),
    m_dynWordWrapSet(false),
    m_dynWordWrapIndicatorsSet(false),
    m_dynWordWrapAlignIndentSet(false),
    m_lineNumbersSet(false),
    m_scrollBarMarksSet(false),
    m_scrollBarPreviewSet(false),
    m_scrollBarMiniMapSet(false),
    m_scrollBarMiniMapAllSet(false),
    m_scrollBarMiniMapWidthSet(false),
    m_showScrollbarsSet(false),
    m_iconBarSet(false),
    m_foldingBarSet(false),
    m_foldingPreviewSet(false),
    m_lineModificationSet(false),
    m_bookmarkSortSet(false),
    m_autoCenterLinesSet(false),
    m_searchFlagsSet(false),
    m_defaultMarkTypeSet(false),
    m_persistentSelectionSet(false),
    m_inputModeSet(false),
    m_viInputModeStealKeysSet(false),
    m_viRelativeLineNumbersSet(false),
    m_automaticCompletionInvocationSet(false),
    m_wordCompletionSet(false),
    m_keywordCompletionSet(false),
    m_wordCompletionMinimalWordLengthSet(false),
    m_smartCopyCutSet(false),
    m_scrollPastEndSet(false),
    m_allowMarkMenu(true),
    m_wordCompletionRemoveTailSet(false),
    m_foldFirstLineSet(false),
    m_autoBracketsSet(false),
    m_backspaceRemoveComposedSet(false),
    m_view(view)
{
}

KateViewConfig::~KateViewConfig()
{
}

namespace
{
const char KEY_SEARCH_REPLACE_FLAGS[] = "Search/Replace Flags";
const char KEY_DYN_WORD_WRAP[] = "Dynamic Word Wrap";
const char KEY_DYN_WORD_WRAP_INDICATORS[] = "Dynamic Word Wrap Indicators";
const char KEY_DYN_WORD_WRAP_ALIGN_INDENT[] = "Dynamic Word Wrap Align Indent";
const char KEY_LINE_NUMBERS[] = "Line Numbers";
const char KEY_SCROLL_BAR_MARKS[] = "Scroll Bar Marks";
const char KEY_SCROLL_BAR_PREVIEW[] = "Scroll Bar Preview";
const char KEY_SCROLL_BAR_MINI_MAP[] = "Scroll Bar MiniMap";
const char KEY_SCROLL_BAR_MINI_MAP_ALL[] = "Scroll Bar Mini Map All";
const char KEY_SCROLL_BAR_MINI_MAP_WIDTH[] = "Scroll Bar Mini Map Width";
const char KEY_SHOW_SCROLLBARS[] = "Show Scrollbars";
const char KEY_ICON_BAR[] = "Icon Bar";
const char KEY_FOLDING_BAR[] = "Folding Bar";
const char KEY_FOLDING_PREVIEW[] = "Folding Preview";
const char KEY_LINE_MODIFICATION[] = "Line Modification";
const char KEY_BOOKMARK_SORT[] = "Bookmark Menu Sorting";
const char KEY_AUTO_CENTER_LINES[] = "Auto Center Lines";
const char KEY_MAX_HISTORY_SIZE[] = "Maximum Search History Size";
const char KEY_DEFAULT_MARK_TYPE[] = "Default Mark Type";
const char KEY_ALLOW_MARK_MENU[] = "Allow Mark Menu";
const char KEY_PERSISTENT_SELECTION[] = "Persistent Selection";
const char KEY_MULTIPLE_SELECTION[] = "Multiple Selection";
const char KEY_INPUT_MODE[] = "Input Mode";
const char KEY_VI_INPUT_MODE_STEAL_KEYS[] = "Vi Input Mode Steal Keys";
const char KEY_VI_RELATIVE_LINE_NUMBERS[] = "Vi Relative Line Numbers";
const char KEY_AUTOMATIC_COMPLETION_INVOCATION[] = "Auto Completion";
const char KEY_WORD_COMPLETION[] = "Word Completion";
const char KEY_KEYWORD_COMPLETION[] = "Keyword Completion";
const char KEY_WORD_COMPLETION_MINIMAL_WORD_LENGTH[] = "Word Completion Minimal Word Length";
const char KEY_WORD_COMPLETION_REMOVE_TAIL[] = "Word Completion Remove Tail";
const char KEY_SMART_COPY_CUT[] = "Smart Copy Cut";
const char KEY_SCROLL_PAST_END[] = "Scroll Past End";
const char KEY_FOLD_FIRST_LINE[] = "Fold First Line";
const char KEY_SHOW_LINES_COUNT[] = "Show Lines Count";
const char KEY_SHOW_WORD_COUNT[] = "Show Word Count";
const char KEY_AUTO_BRACKETS[] = "Auto Brackets";
const char KEY_BACKSPACE_REMOVE_COMPOSED[] = "Backspace Remove Composed Characters";
}

void KateViewConfig::readConfig(const KConfigGroup &config)
{
    configStart();

    // default on
    setDynWordWrap(config.readEntry(KEY_DYN_WORD_WRAP, true));
    setDynWordWrapIndicators(config.readEntry(KEY_DYN_WORD_WRAP_INDICATORS, 1));
    setDynWordWrapAlignIndent(config.readEntry(KEY_DYN_WORD_WRAP_ALIGN_INDENT, 80));

    setLineNumbers(config.readEntry(KEY_LINE_NUMBERS, false));

    setScrollBarMarks(config.readEntry(KEY_SCROLL_BAR_MARKS, false));

    setScrollBarPreview(config.readEntry(KEY_SCROLL_BAR_PREVIEW, true));

    setScrollBarMiniMap(config.readEntry(KEY_SCROLL_BAR_MINI_MAP, true));

    setScrollBarMiniMapAll(config.readEntry(KEY_SCROLL_BAR_MINI_MAP_ALL, false));

    setScrollBarMiniMapWidth(config.readEntry(KEY_SCROLL_BAR_MINI_MAP_WIDTH,  60));

    setShowScrollbars(config.readEntry(KEY_SHOW_SCROLLBARS, static_cast<int>(AlwaysOn)));

    setIconBar(config.readEntry(KEY_ICON_BAR, false));

    setFoldingBar(config.readEntry(KEY_FOLDING_BAR, true));

    setFoldingPreview(config.readEntry(KEY_FOLDING_PREVIEW, true));

    setLineModification(config.readEntry(KEY_LINE_MODIFICATION, false));

    setBookmarkSort(config.readEntry(KEY_BOOKMARK_SORT, 0));

    setAutoCenterLines(config.readEntry(KEY_AUTO_CENTER_LINES, 0));

    setSearchFlags(config.readEntry(KEY_SEARCH_REPLACE_FLAGS,
                                    IncFromCursor | PowerMatchCase | PowerModePlainText));

    m_maxHistorySize = config.readEntry(KEY_MAX_HISTORY_SIZE, 100);

    setDefaultMarkType(config.readEntry(KEY_DEFAULT_MARK_TYPE, int(KTextEditor::MarkInterface::markType01)));
    setAllowMarkMenu(config.readEntry(KEY_ALLOW_MARK_MENU, true));

    setPersistentSelection(config.readEntry(KEY_PERSISTENT_SELECTION, false));
    setMultipleSelection(config.readEntry(KEY_MULTIPLE_SELECTION, false));

    setInputModeRaw(config.readEntry(KEY_INPUT_MODE, 0));
    setViInputModeStealKeys(config.readEntry(KEY_VI_INPUT_MODE_STEAL_KEYS, false));
    setViRelativeLineNumbers(config.readEntry(KEY_VI_RELATIVE_LINE_NUMBERS, false));

    setAutomaticCompletionInvocation(config.readEntry(KEY_AUTOMATIC_COMPLETION_INVOCATION, true));
    setWordCompletion(config.readEntry(KEY_WORD_COMPLETION, true));
    setKeywordCompletion(config.readEntry(KEY_KEYWORD_COMPLETION, true));
    setWordCompletionMinimalWordLength(config.readEntry(KEY_WORD_COMPLETION_MINIMAL_WORD_LENGTH, 3));
    setWordCompletionRemoveTail(config.readEntry(KEY_WORD_COMPLETION_REMOVE_TAIL, true));
    setSmartCopyCut(config.readEntry(KEY_SMART_COPY_CUT, false));
    setScrollPastEnd(config.readEntry(KEY_SCROLL_PAST_END, false));
    setFoldFirstLine(config.readEntry(KEY_FOLD_FIRST_LINE, false));
    setShowLinesCount(config.readEntry(KEY_SHOW_LINES_COUNT, false));
    setShowWordCount(config.readEntry(KEY_SHOW_WORD_COUNT, false));
    setAutoBrackets(config.readEntry(KEY_AUTO_BRACKETS, false));

    setBackspaceRemoveComposed(config.readEntry(KEY_BACKSPACE_REMOVE_COMPOSED, false));

    configEnd();
}

void KateViewConfig::writeConfig(KConfigGroup &config)
{
    config.writeEntry(KEY_DYN_WORD_WRAP, dynWordWrap());
    config.writeEntry(KEY_DYN_WORD_WRAP_INDICATORS, dynWordWrapIndicators());
    config.writeEntry(KEY_DYN_WORD_WRAP_ALIGN_INDENT, dynWordWrapAlignIndent());

    config.writeEntry(KEY_LINE_NUMBERS, lineNumbers());

    config.writeEntry(KEY_SCROLL_BAR_MARKS, scrollBarMarks());

    config.writeEntry(KEY_SCROLL_BAR_PREVIEW, scrollBarPreview());

    config.writeEntry(KEY_SCROLL_BAR_MINI_MAP, scrollBarMiniMap());

    config.writeEntry(KEY_SCROLL_BAR_MINI_MAP_ALL, scrollBarMiniMapAll());

    config.writeEntry(KEY_SCROLL_BAR_MINI_MAP_WIDTH, scrollBarMiniMapWidth());

    config.writeEntry(KEY_SHOW_SCROLLBARS, showScrollbars());

    config.writeEntry(KEY_ICON_BAR, iconBar());

    config.writeEntry(KEY_FOLDING_BAR, foldingBar());

    config.writeEntry(KEY_FOLDING_PREVIEW, foldingPreview());

    config.writeEntry(KEY_LINE_MODIFICATION, lineModification());

    config.writeEntry(KEY_BOOKMARK_SORT, bookmarkSort());

    config.writeEntry(KEY_AUTO_CENTER_LINES, autoCenterLines());

    config.writeEntry(KEY_SEARCH_REPLACE_FLAGS, int(searchFlags()));

    config.writeEntry(KEY_MAX_HISTORY_SIZE, m_maxHistorySize);

    config.writeEntry(KEY_DEFAULT_MARK_TYPE, defaultMarkType());

    config.writeEntry(KEY_ALLOW_MARK_MENU, allowMarkMenu());

    config.writeEntry(KEY_PERSISTENT_SELECTION, persistentSelection());
    config.writeEntry(KEY_MULTIPLE_SELECTION, multipleSelection());

    config.writeEntry(KEY_AUTOMATIC_COMPLETION_INVOCATION, automaticCompletionInvocation());
    config.writeEntry(KEY_WORD_COMPLETION, wordCompletion());
    config.writeEntry(KEY_KEYWORD_COMPLETION, keywordCompletion());
    config.writeEntry(KEY_WORD_COMPLETION_MINIMAL_WORD_LENGTH, wordCompletionMinimalWordLength());
    config.writeEntry(KEY_WORD_COMPLETION_REMOVE_TAIL, wordCompletionRemoveTail());

    config.writeEntry(KEY_SMART_COPY_CUT, smartCopyCut());
    config.writeEntry(KEY_SCROLL_PAST_END, scrollPastEnd());
    config.writeEntry(KEY_FOLD_FIRST_LINE, foldFirstLine());

    config.writeEntry(KEY_INPUT_MODE, static_cast<int>(inputMode()));
    config.writeEntry(KEY_VI_INPUT_MODE_STEAL_KEYS, viInputModeStealKeys());
    config.writeEntry(KEY_VI_RELATIVE_LINE_NUMBERS, viRelativeLineNumbers());

    config.writeEntry(KEY_SHOW_LINES_COUNT, showLinesCount());
    config.writeEntry(KEY_SHOW_WORD_COUNT, showWordCount());
    config.writeEntry(KEY_AUTO_BRACKETS, autoBrackets());

    config.writeEntry(KEY_BACKSPACE_REMOVE_COMPOSED, backspaceRemoveComposed());
}

void KateViewConfig::updateConfig()
{
    if (m_view) {
        m_view->updateConfig();
        return;
    }

    if (isGlobal()) {
        foreach (KTextEditor::ViewPrivate* view, KTextEditor::EditorPrivate::self()->views()) {
            view->updateConfig();
        }

        // write config
        KConfigGroup cg(KTextEditor::EditorPrivate::config(), "View");
        writeConfig(cg);
        KTextEditor::EditorPrivate::config()->sync();
    }
}

bool KateViewConfig::dynWordWrap() const
{
    if (m_dynWordWrapSet || isGlobal()) {
        return m_dynWordWrap;
    }

    return s_global->dynWordWrap();
}

void KateViewConfig::setDynWordWrap(bool wrap)
{
    if (m_dynWordWrapSet && m_dynWordWrap == wrap) {
        return;
    }

    configStart();

    m_dynWordWrapSet = true;
    m_dynWordWrap = wrap;

    configEnd();
}

int KateViewConfig::dynWordWrapIndicators() const
{
    if (m_dynWordWrapIndicatorsSet || isGlobal()) {
        return m_dynWordWrapIndicators;
    }

    return s_global->dynWordWrapIndicators();
}

void KateViewConfig::setDynWordWrapIndicators(int mode)
{
    if (m_dynWordWrapIndicatorsSet && m_dynWordWrapIndicators == mode) {
        return;
    }

    configStart();

    m_dynWordWrapIndicatorsSet = true;
    m_dynWordWrapIndicators = qBound(0, mode, 80);

    configEnd();
}

int KateViewConfig::dynWordWrapAlignIndent() const
{
    if (m_dynWordWrapAlignIndentSet || isGlobal()) {
        return m_dynWordWrapAlignIndent;
    }

    return s_global->dynWordWrapAlignIndent();
}

void KateViewConfig::setDynWordWrapAlignIndent(int indent)
{
    if (m_dynWordWrapAlignIndentSet && m_dynWordWrapAlignIndent == indent) {
        return;
    }

    configStart();

    m_dynWordWrapAlignIndentSet = true;
    m_dynWordWrapAlignIndent = indent;

    configEnd();
}

bool KateViewConfig::lineNumbers() const
{
    if (m_lineNumbersSet || isGlobal()) {
        return m_lineNumbers;
    }

    return s_global->lineNumbers();
}

void KateViewConfig::setLineNumbers(bool on)
{
    if (m_lineNumbersSet && m_lineNumbers == on) {
        return;
    }

    configStart();

    m_lineNumbersSet = true;
    m_lineNumbers = on;

    configEnd();
}

bool KateViewConfig::scrollBarMarks() const
{
    if (m_scrollBarMarksSet || isGlobal()) {
        return m_scrollBarMarks;
    }

    return s_global->scrollBarMarks();
}

void KateViewConfig::setScrollBarMarks(bool on)
{
    if (m_scrollBarMarksSet && m_scrollBarMarks == on) {
        return;
    }

    configStart();

    m_scrollBarMarksSet = true;
    m_scrollBarMarks = on;

    configEnd();
}

bool KateViewConfig::scrollBarPreview() const
{
    if (m_scrollBarPreviewSet || isGlobal()) {
        return m_scrollBarPreview;
    }

    return s_global->scrollBarPreview();
}

void KateViewConfig::setScrollBarPreview(bool on)
{
    if (m_scrollBarPreviewSet && m_scrollBarPreview == on) {
        return;
    }

    configStart();

    m_scrollBarPreviewSet = true;
    m_scrollBarPreview = on;

    configEnd();
}

bool KateViewConfig::scrollBarMiniMap() const
{
    if (m_scrollBarMiniMapSet || isGlobal()) {
        return m_scrollBarMiniMap;
    }

    return s_global->scrollBarMiniMap();
}

void KateViewConfig::setScrollBarMiniMap(bool on)
{
    if (m_scrollBarMiniMapSet && m_scrollBarMiniMap == on) {
        return;
    }

    configStart();

    m_scrollBarMiniMapSet = true;
    m_scrollBarMiniMap = on;

    configEnd();
}

bool KateViewConfig::scrollBarMiniMapAll() const
{
    if (m_scrollBarMiniMapAllSet || isGlobal()) {
        return m_scrollBarMiniMapAll;
    }

    return s_global->scrollBarMiniMapAll();
}

void KateViewConfig::setScrollBarMiniMapAll(bool on)
{
    if (m_scrollBarMiniMapAllSet && m_scrollBarMiniMapAll == on) {
        return;
    }

    configStart();

    m_scrollBarMiniMapAllSet = true;
    m_scrollBarMiniMapAll = on;

    configEnd();
}

int KateViewConfig::scrollBarMiniMapWidth() const
{
    if (m_scrollBarMiniMapWidthSet || isGlobal()) {
        return m_scrollBarMiniMapWidth;
    }

    return s_global->scrollBarMiniMapWidth();
}

void KateViewConfig::setScrollBarMiniMapWidth(int width)
{
    if (m_scrollBarMiniMapWidthSet && m_scrollBarMiniMapWidth == width) {
        return;
    }

    configStart();

    m_scrollBarMiniMapWidthSet = true;
    m_scrollBarMiniMapWidth = width;

    configEnd();
}

int KateViewConfig::showScrollbars() const
{
    if (m_showScrollbarsSet || isGlobal()) {
        return m_showScrollbars;
    }

    return s_global->showScrollbars();
}

void KateViewConfig::setShowScrollbars(int mode)
{
    if (m_showScrollbarsSet && m_showScrollbars == mode) {
        return;
    }

    configStart();

    m_showScrollbarsSet = true;
    m_showScrollbars = qBound(0, mode, 80);

    configEnd();
}

bool KateViewConfig::autoBrackets() const
{
    if (m_autoBracketsSet || isGlobal()) {
        return m_autoBrackets;
    }

    return s_global->autoBrackets();
}

void KateViewConfig::setAutoBrackets(bool on)
{
    if (m_autoBracketsSet && m_autoBrackets == on) {
        return;
    }

    configStart();

    m_autoBracketsSet = true;
    m_autoBrackets = on;

    configEnd();
}

bool KateViewConfig::iconBar() const
{
    if (m_iconBarSet || isGlobal()) {
        return m_iconBar;
    }

    return s_global->iconBar();
}

void KateViewConfig::setIconBar(bool on)
{
    if (m_iconBarSet && m_iconBar == on) {
        return;
    }

    configStart();

    m_iconBarSet = true;
    m_iconBar = on;

    configEnd();
}

bool KateViewConfig::foldingBar() const
{
    if (m_foldingBarSet || isGlobal()) {
        return m_foldingBar;
    }

    return s_global->foldingBar();
}

void KateViewConfig::setFoldingBar(bool on)
{
    if (m_foldingBarSet && m_foldingBar == on) {
        return;
    }

    configStart();

    m_foldingBarSet = true;
    m_foldingBar = on;

    configEnd();
}

bool KateViewConfig::foldingPreview() const
{
    if (m_foldingPreviewSet || isGlobal()) {
        return m_foldingPreview;
    }

    return s_global->foldingPreview();
}

void KateViewConfig::setFoldingPreview(bool on)
{
    if (m_foldingPreviewSet && m_foldingPreview == on) {
        return;
    }

    configStart();

    m_foldingPreviewSet = true;
    m_foldingPreview = on;

    configEnd();
}

bool KateViewConfig::lineModification() const
{
    if (m_lineModificationSet || isGlobal()) {
        return m_lineModification;
    }

    return s_global->lineModification();
}

void KateViewConfig::setLineModification(bool on)
{
    if (m_lineModificationSet && m_lineModification == on) {
        return;
    }

    configStart();

    m_lineModificationSet = true;
    m_lineModification = on;

    configEnd();
}

int KateViewConfig::bookmarkSort() const
{
    if (m_bookmarkSortSet || isGlobal()) {
        return m_bookmarkSort;
    }

    return s_global->bookmarkSort();
}

void KateViewConfig::setBookmarkSort(int mode)
{
    if (m_bookmarkSortSet && m_bookmarkSort == mode) {
        return;
    }

    configStart();

    m_bookmarkSortSet = true;
    m_bookmarkSort = mode;

    configEnd();
}

int KateViewConfig::autoCenterLines() const
{
    if (m_autoCenterLinesSet || isGlobal()) {
        return m_autoCenterLines;
    }

    return s_global->autoCenterLines();
}

void KateViewConfig::setAutoCenterLines(int lines)
{
    if (lines < 0) {
        return;
    }

    if (m_autoCenterLinesSet && m_autoCenterLines == lines) {
        return;
    }

    configStart();

    m_autoCenterLinesSet = true;
    m_autoCenterLines = lines;

    configEnd();
}

long KateViewConfig::searchFlags() const
{
    if (m_searchFlagsSet || isGlobal()) {
        return m_searchFlags;
    }

    return s_global->searchFlags();
}

void KateViewConfig::setSearchFlags(long flags)
{
    if (m_searchFlagsSet && m_searchFlags == flags) {
        return;
    }

    configStart();

    m_searchFlagsSet = true;
    m_searchFlags = flags;

    configEnd();
}

int KateViewConfig::maxHistorySize() const
{
    return m_maxHistorySize;
}

uint KateViewConfig::defaultMarkType() const
{
    if (m_defaultMarkTypeSet || isGlobal()) {
        return m_defaultMarkType;
    }

    return s_global->defaultMarkType();
}

void KateViewConfig::setDefaultMarkType(uint type)
{
    if (m_defaultMarkTypeSet && m_defaultMarkType == type) {
        return;
    }

    configStart();

    m_defaultMarkTypeSet = true;
    m_defaultMarkType = type;

    configEnd();
}

bool KateViewConfig::allowMarkMenu() const
{
    return m_allowMarkMenu;
}

void KateViewConfig::setAllowMarkMenu(bool allow)
{
    m_allowMarkMenu = allow;
}

bool KateViewConfig::persistentSelection() const
{
    if (m_persistentSelectionSet || isGlobal()) {
        return m_persistentSelection;
    }

    return s_global->persistentSelection();
}

void KateViewConfig::setPersistentSelection(bool on)
{
    if (m_persistentSelectionSet && m_persistentSelection == on) {
        return;
    }

    configStart();

    m_persistentSelectionSet = true;
    m_persistentSelection = on;

    configEnd();
}

bool KateViewConfig::multipleSelection() const
{
    if (m_multipleSelectionSet || isGlobal()) {
        return m_multipleSelection;
    }

    return s_global->multipleSelection();
}

void KateViewConfig::setMultipleSelection(bool on)
{
    if (m_multipleSelectionSet && m_multipleSelection == on) {
        return;
    }

    configStart();

    m_multipleSelectionSet = true;
    m_multipleSelection = on;

    configEnd();
}

KTextEditor::View::InputMode KateViewConfig::inputMode() const
{
    if (m_inputModeSet || isGlobal()) {
        return m_inputMode;
    }

    return s_global->inputMode();
}

void KateViewConfig::setInputMode(KTextEditor::View::InputMode mode)
{
    if (m_inputModeSet && m_inputMode == mode) {
        return;
    }

    configStart();

    m_inputModeSet = true;
    m_inputMode = mode;

    configEnd();
}

void KateViewConfig::setInputModeRaw(int rawmode)
{
    setInputMode(static_cast<KTextEditor::View::InputMode>(rawmode));
}

bool KateViewConfig::viInputModeStealKeys() const
{
    if (m_viInputModeStealKeysSet || isGlobal()) {
        return m_viInputModeStealKeys;
    }

    return s_global->viInputModeStealKeys();
}

void KateViewConfig::setViInputModeStealKeys(bool on)
{
    if (m_viInputModeStealKeysSet && m_viInputModeStealKeys == on) {
        return;
    }

    configStart();
    m_viInputModeStealKeysSet = true;
    m_viInputModeStealKeys = on;
    configEnd();
}

bool KateViewConfig::viRelativeLineNumbers() const
{
    if (m_viRelativeLineNumbersSet  || isGlobal()) {
        return m_viRelativeLineNumbers;
    }

    return s_global->viRelativeLineNumbers();
}

void KateViewConfig::setViRelativeLineNumbers(bool on)
{
    if (m_viRelativeLineNumbersSet && m_viRelativeLineNumbers ==  on) {
        return;
    }

    configStart();
    m_viRelativeLineNumbersSet = true;
    m_viRelativeLineNumbers = on;
    configEnd();
}

bool KateViewConfig::automaticCompletionInvocation() const
{
    if (m_automaticCompletionInvocationSet || isGlobal()) {
        return m_automaticCompletionInvocation;
    }

    return s_global->automaticCompletionInvocation();
}

void KateViewConfig::setAutomaticCompletionInvocation(bool on)
{
    if (m_automaticCompletionInvocationSet && m_automaticCompletionInvocation == on) {
        return;
    }

    configStart();
    m_automaticCompletionInvocationSet = true;
    m_automaticCompletionInvocation = on;
    configEnd();
}

bool KateViewConfig::wordCompletion() const
{
    if (m_wordCompletionSet || isGlobal()) {
        return m_wordCompletion;
    }

    return s_global->wordCompletion();
}

void KateViewConfig::setWordCompletion(bool on)
{
    if (m_wordCompletionSet && m_wordCompletion == on) {
        return;
    }

    configStart();
    m_wordCompletionSet = true;
    m_wordCompletion = on;
    configEnd();
}

bool KateViewConfig::keywordCompletion() const
{
    if (m_keywordCompletionSet || isGlobal())
        return m_keywordCompletion;
    return s_global->keywordCompletion();
}

void KateViewConfig::setKeywordCompletion(bool on)
{
    if (m_keywordCompletionSet && m_keywordCompletion == on)
        return;
    configStart();
    m_keywordCompletionSet = true;
    m_keywordCompletion = on;
    configEnd();
}

int KateViewConfig::wordCompletionMinimalWordLength() const
{
    if (m_wordCompletionMinimalWordLengthSet || isGlobal()) {
        return m_wordCompletionMinimalWordLength;
    }

    return s_global->wordCompletionMinimalWordLength();
}

void KateViewConfig::setWordCompletionMinimalWordLength(int length)
{
    if (m_wordCompletionMinimalWordLengthSet && m_wordCompletionMinimalWordLength == length) {
        return;
    }

    configStart();

    m_wordCompletionMinimalWordLengthSet = true;
    m_wordCompletionMinimalWordLength = length;

    configEnd();
}

bool KateViewConfig::wordCompletionRemoveTail() const
{
    if (m_wordCompletionRemoveTailSet || isGlobal()) {
        return m_wordCompletionRemoveTail;
    }

    return s_global->wordCompletionRemoveTail();
}

void KateViewConfig::setWordCompletionRemoveTail(bool on)
{
    if (m_wordCompletionRemoveTailSet && m_wordCompletionRemoveTail == on) {
        return;
    }

    configStart();
    m_wordCompletionRemoveTailSet = true;
    m_wordCompletionRemoveTail = on;
    configEnd();
}

bool KateViewConfig::smartCopyCut() const
{
    if (m_smartCopyCutSet || isGlobal()) {
        return m_smartCopyCut;
    }

    return s_global->smartCopyCut();
}

void KateViewConfig::setSmartCopyCut(bool on)
{
    if (m_smartCopyCutSet && m_smartCopyCut == on) {
        return;
    }

    configStart();

    m_smartCopyCutSet = true;
    m_smartCopyCut = on;

    configEnd();
}

bool KateViewConfig::scrollPastEnd() const
{
    if (m_scrollPastEndSet || isGlobal()) {
        return m_scrollPastEnd;
    }

    return s_global->scrollPastEnd();
}

void KateViewConfig::setScrollPastEnd(bool on)
{
    if (m_scrollPastEndSet && m_scrollPastEnd == on) {
        return;
    }

    configStart();

    m_scrollPastEndSet = true;
    m_scrollPastEnd = on;

    configEnd();
}

bool KateViewConfig::foldFirstLine() const
{
    if (m_foldFirstLineSet || isGlobal()) {
        return m_foldFirstLine;
    }

    return s_global->foldFirstLine();
}

void KateViewConfig::setFoldFirstLine(bool on)
{
    if (m_foldFirstLineSet && m_foldFirstLine == on) {
        return;
    }

    configStart();

    m_foldFirstLineSet = true;
    m_foldFirstLine = on;

    configEnd();
}

bool KateViewConfig::showWordCount() const
{
    if (m_showWordCountSet || isGlobal()) {
        return m_showWordCount;
    }

    return s_global->showWordCount();
}

void KateViewConfig::setShowWordCount(bool on)
{
    if (m_showWordCountSet && m_showWordCount == on) {
        return;
    }

    configStart();
    m_showWordCountSet = true;
    m_showWordCount = on;
    configEnd();
}

bool KateViewConfig::showLinesCount() const
{
    if (m_showLinesCountSet || isGlobal()) {
        return m_showLinesCount;
    }

    return s_global->showLinesCount();
}

void KateViewConfig::setShowLinesCount(bool on)
{
    if (m_showLinesCountSet && m_showLinesCount == on) {
        return;
    }

    configStart();
    m_showLinesCountSet = true;
    m_showLinesCount = on;
    configEnd();
}

bool KateViewConfig::backspaceRemoveComposed() const
{
    if (m_backspaceRemoveComposedSet || isGlobal()) {
        return m_backspaceRemoveComposed;
    }

    return s_global->backspaceRemoveComposed();
}

void KateViewConfig::setBackspaceRemoveComposed(bool on)
{
    if (m_backspaceRemoveComposedSet && m_backspaceRemoveComposed == on) {
        return;
    }

    configStart();

    m_backspaceRemoveComposedSet = true;
    m_backspaceRemoveComposed = on;

    configEnd();
}

//END

//BEGIN KateRendererConfig
KateRendererConfig::KateRendererConfig()
    : m_fontMetrics(QFont()),
      m_lineMarkerColor(KTextEditor::MarkInterface::reservedMarkersCount()),

      m_schemaSet(false),
      m_fontSet(false),
      m_wordWrapMarkerSet(false),
      m_showIndentationLinesSet(false),
      m_showWholeBracketExpressionSet(false),
      m_backgroundColorSet(false),
      m_selectionColorSet(false),
      m_highlightedLineColorSet(false),
      m_highlightedBracketColorSet(false),
      m_wordWrapMarkerColorSet(false),
      m_tabMarkerColorSet(false),
      m_indentationLineColorSet(false),
      m_iconBarColorSet(false),
      m_foldingColorSet(false),
      m_lineNumberColorSet(false),
      m_currentLineNumberColorSet(false),
      m_separatorColorSet(false),
      m_spellingMistakeLineColorSet(false),
      m_templateColorsSet(false),
      m_modifiedLineColorSet(false),
      m_savedLineColorSet(false),
      m_searchHighlightColorSet(false),
      m_replaceHighlightColorSet(false),
      m_lineMarkerColorSet(m_lineMarkerColor.size())

{
    // init bitarray
    m_lineMarkerColorSet.fill(true);

    s_global = this;

    // init with defaults from config or really hardcoded ones
    KConfigGroup config(KTextEditor::EditorPrivate::config(), "Renderer");
    readConfig(config);
}

KateRendererConfig::KateRendererConfig(KateRenderer *renderer)
    : m_fontMetrics(QFont()),
      m_lineMarkerColor(KTextEditor::MarkInterface::reservedMarkersCount()),
      m_schemaSet(false),
      m_fontSet(false),
      m_wordWrapMarkerSet(false),
      m_showIndentationLinesSet(false),
      m_showWholeBracketExpressionSet(false),
      m_backgroundColorSet(false),
      m_selectionColorSet(false),
      m_highlightedLineColorSet(false),
      m_highlightedBracketColorSet(false),
      m_wordWrapMarkerColorSet(false),
      m_tabMarkerColorSet(false),
      m_indentationLineColorSet(false),
      m_iconBarColorSet(false),
      m_foldingColorSet(false),
      m_lineNumberColorSet(false),
      m_currentLineNumberColorSet(false),
      m_separatorColorSet(false),
      m_spellingMistakeLineColorSet(false),
      m_templateColorsSet(false),
      m_modifiedLineColorSet(false),
      m_savedLineColorSet(false),
      m_searchHighlightColorSet(false),
      m_replaceHighlightColorSet(false),
      m_lineMarkerColorSet(m_lineMarkerColor.size()),
      m_renderer(renderer)
{
    // init bitarray
    m_lineMarkerColorSet.fill(false);
}

KateRendererConfig::~KateRendererConfig()
{
}

namespace
{
const char KEY_SCHEMA[] = "Schema";
const char KEY_WORD_WRAP_MARKER[] = "Word Wrap Marker";
const char KEY_SHOW_INDENTATION_LINES[] = "Show Indentation Lines";
const char KEY_SHOW_WHOLE_BRACKET_EXPRESSION[] = "Show Whole Bracket Expression";
const char KEY_ANIMATE_BRACKET_MATCHING[] = "Animate Bracket Matching";
}

void KateRendererConfig::readConfig(const KConfigGroup &config)
{
    configStart();

    // "Normal" Schema MUST BE THERE, see global kateschemarc
    setSchema(config.readEntry(KEY_SCHEMA, "Normal"));

    setWordWrapMarker(config.readEntry(KEY_WORD_WRAP_MARKER, false));

    setShowIndentationLines(config.readEntry(KEY_SHOW_INDENTATION_LINES, false));

    setShowWholeBracketExpression(config.readEntry(KEY_SHOW_WHOLE_BRACKET_EXPRESSION, false));

    setAnimateBracketMatching(config.readEntry(KEY_ANIMATE_BRACKET_MATCHING, false));

    configEnd();
}

void KateRendererConfig::writeConfig(KConfigGroup &config)
{
    config.writeEntry(KEY_SCHEMA, schema());

    config.writeEntry(KEY_WORD_WRAP_MARKER, wordWrapMarker());

    config.writeEntry(KEY_SHOW_INDENTATION_LINES, showIndentationLines());

    config.writeEntry(KEY_SHOW_WHOLE_BRACKET_EXPRESSION, showWholeBracketExpression());

    config.writeEntry(KEY_ANIMATE_BRACKET_MATCHING, animateBracketMatching());
}

void KateRendererConfig::updateConfig()
{
    if (m_renderer) {
        m_renderer->updateConfig();
        return;
    }

    if (isGlobal()) {
        for (int z = 0; z < KTextEditor::EditorPrivate::self()->views().size(); ++z) {
            (KTextEditor::EditorPrivate::self()->views())[z]->renderer()->updateConfig();
        }

        // write config
        KConfigGroup cg(KTextEditor::EditorPrivate::config(), "Renderer");
        writeConfig(cg);
        KTextEditor::EditorPrivate::config()->sync();
    }
}

const QString &KateRendererConfig::schema() const
{
    if (m_schemaSet || isGlobal()) {
        return m_schema;
    }

    return s_global->schema();
}

void KateRendererConfig::setSchema(const QString &schema)
{
    if (m_schemaSet && m_schema == schema) {
        return;
    }

    configStart();
    m_schemaSet = true;
    m_schema = schema;
    setSchemaInternal(schema);
    configEnd();
}

void KateRendererConfig::reloadSchema()
{
    if (isGlobal()) {
        setSchemaInternal(m_schema);
        foreach (KTextEditor::ViewPrivate *view, KTextEditor::EditorPrivate::self()->views()) {
            view->renderer()->config()->reloadSchema();
        }
    }

    else if (m_renderer && m_schemaSet) {
        setSchemaInternal(m_schema);
    }

    // trigger renderer/view update
    if (m_renderer) {
        m_renderer->updateConfig();
    }
}

void KateRendererConfig::setSchemaInternal(const QString &schema)
{
    m_schemaSet = true;
    m_schema = schema;

    KConfigGroup config = KTextEditor::EditorPrivate::self()->schemaManager()->schema(schema);

    // use global color instance, creation is expensive!
    const KateDefaultColors &colors(KTextEditor::EditorPrivate::self()->defaultColors());

    m_backgroundColor = config.readEntry("Color Background", colors.color(Kate::Background));

    m_backgroundColorSet = true;
    m_selectionColor = config.readEntry("Color Selection", colors.color(Kate::SelectionBackground));
    m_selectionColorSet = true;
    m_highlightedLineColor  = config.readEntry("Color Highlighted Line", colors.color(Kate::HighlightedLineBackground));
    m_highlightedLineColorSet = true;
    m_highlightedBracketColor = config.readEntry("Color Highlighted Bracket", colors.color(Kate::HighlightedBracket));
    m_highlightedBracketColorSet = true;
    m_wordWrapMarkerColor = config.readEntry("Color Word Wrap Marker", colors.color(Kate::WordWrapMarker));
    m_wordWrapMarkerColorSet = true;
    m_tabMarkerColor = config.readEntry("Color Tab Marker", colors.color(Kate::TabMarker));
    m_tabMarkerColorSet = true;
    m_indentationLineColor = config.readEntry("Color Indentation Line", colors.color(Kate::IndentationLine));
    m_indentationLineColorSet = true;
    m_iconBarColor  = config.readEntry("Color Icon Bar", colors.color(Kate::IconBar));
    m_iconBarColorSet = true;
    m_foldingColor  = config.readEntry("Color Code Folding", colors.color(Kate::CodeFolding));
    m_foldingColorSet = true;
    m_lineNumberColor = config.readEntry("Color Line Number", colors.color(Kate::LineNumber));
    m_lineNumberColorSet = true;
    m_currentLineNumberColor = config.readEntry("Color Current Line Number", colors.color(Kate::CurrentLineNumber));
    m_currentLineNumberColorSet = true;
    m_separatorColor = config.readEntry("Color Separator", colors.color(Kate::Separator));
    m_separatorColorSet = true;
    m_spellingMistakeLineColor = config.readEntry("Color Spelling Mistake Line", colors.color(Kate::SpellingMistakeLine));
    m_spellingMistakeLineColorSet = true;

    m_modifiedLineColor = config.readEntry("Color Modified Lines", colors.color(Kate::ModifiedLine));
    m_modifiedLineColorSet = true;
    m_savedLineColor = config.readEntry("Color Saved Lines", colors.color(Kate::SavedLine));
    m_savedLineColorSet = true;
    m_searchHighlightColor = config.readEntry("Color Search Highlight", colors.color(Kate::SearchHighlight));
    m_searchHighlightColorSet = true;
    m_replaceHighlightColor = config.readEntry("Color Replace Highlight", colors.color(Kate::ReplaceHighlight));
    m_replaceHighlightColorSet = true;

    for (int i = Kate::FIRST_MARK; i <= Kate::LAST_MARK; i++) {
        QColor col = config.readEntry(QStringLiteral("Color MarkType %1").arg(i + 1), colors.mark(i));
        m_lineMarkerColorSet[i] = true;
        m_lineMarkerColor[i] = col;
    }

    setFontWithDroppedStyleName(config.readEntry("Font", QFontDatabase::systemFont(QFontDatabase::FixedFont)));

    m_templateBackgroundColor = config.readEntry(QStringLiteral("Color Template Background"), colors.color(Kate::TemplateBackground));

    m_templateFocusedEditablePlaceholderColor = config.readEntry(QStringLiteral("Color Template Focused Editable Placeholder"),
                                                                 colors.color(Kate::TemplateFocusedEditablePlaceholder));

    m_templateEditablePlaceholderColor = config.readEntry(QStringLiteral("Color Template Editable Placeholder"),
                                                          colors.color(Kate::TemplateEditablePlaceholder));

    m_templateNotEditablePlaceholderColor = config.readEntry(QStringLiteral("Color Template Not Editable Placeholder"),
                                                             colors.color(Kate::TemplateNotEditablePlaceholder));

    m_templateColorsSet = true;
}

const QFont &KateRendererConfig::font() const
{
    if (m_fontSet || isGlobal()) {
        return m_font;
    }

    return s_global->font();
}

const QFontMetricsF &KateRendererConfig::fontMetrics() const
{
    if (m_fontSet || isGlobal()) {
        return m_fontMetrics;
    }

    return s_global->fontMetrics();
}

void KateRendererConfig::setFont(const QFont &font)
{
    if (m_fontSet && m_font == font) {
        return;
    }

    configStart();
    setFontWithDroppedStyleName(font);
    configEnd();
}

void KateRendererConfig::setFontWithDroppedStyleName(const QFont &font)
{
    /**
     * Drop styleName, otherwise stuff like bold/italic/... won't work as style!
     */
    m_font = font;
    m_font.setStyleName(QString());
    m_fontMetrics = QFontMetricsF(m_font);
    m_fontSet = true;
}

bool KateRendererConfig::wordWrapMarker() const
{
    if (m_wordWrapMarkerSet || isGlobal()) {
        return m_wordWrapMarker;
    }

    return s_global->wordWrapMarker();
}

void KateRendererConfig::setWordWrapMarker(bool on)
{
    if (m_wordWrapMarkerSet && m_wordWrapMarker == on) {
        return;
    }

    configStart();

    m_wordWrapMarkerSet = true;
    m_wordWrapMarker = on;

    configEnd();
}

const QColor &KateRendererConfig::backgroundColor() const
{
    if (m_backgroundColorSet || isGlobal()) {
        return m_backgroundColor;
    }

    return s_global->backgroundColor();
}

void KateRendererConfig::setBackgroundColor(const QColor &col)
{
    if (m_backgroundColorSet && m_backgroundColor == col) {
        return;
    }

    configStart();

    m_backgroundColorSet = true;
    m_backgroundColor = col;

    configEnd();
}

const QColor &KateRendererConfig::selectionColor() const
{
    if (m_selectionColorSet || isGlobal()) {
        return m_selectionColor;
    }

    return s_global->selectionColor();
}

void KateRendererConfig::setSelectionColor(const QColor &col)
{
    if (m_selectionColorSet && m_selectionColor == col) {
        return;
    }

    configStart();

    m_selectionColorSet = true;
    m_selectionColor = col;

    configEnd();
}

const QColor &KateRendererConfig::highlightedLineColor() const
{
    if (m_highlightedLineColorSet || isGlobal()) {
        return m_highlightedLineColor;
    }

    return s_global->highlightedLineColor();
}

void KateRendererConfig::setHighlightedLineColor(const QColor &col)
{
    if (m_highlightedLineColorSet && m_highlightedLineColor == col) {
        return;
    }

    configStart();

    m_highlightedLineColorSet = true;
    m_highlightedLineColor = col;

    configEnd();
}

const QColor &KateRendererConfig::lineMarkerColor(KTextEditor::MarkInterface::MarkTypes type) const
{
    int index = 0;
    if (type > 0) {
        while ((type >> index++) ^ 1) {}
    }
    index -= 1;

    if (index < 0 || index >= KTextEditor::MarkInterface::reservedMarkersCount()) {
        static QColor dummy;
        return dummy;
    }

    if (m_lineMarkerColorSet[index] || isGlobal()) {
        return m_lineMarkerColor[index];
    }

    return s_global->lineMarkerColor(type);
}

void KateRendererConfig::setLineMarkerColor(const QColor &col, KTextEditor::MarkInterface::MarkTypes type)
{
    int index = static_cast<int>(log(static_cast<double>(type)) / log(2.0));
    Q_ASSERT(index >= 0 && index < KTextEditor::MarkInterface::reservedMarkersCount());

    if (m_lineMarkerColorSet[index] && m_lineMarkerColor[index] == col) {
        return;
    }

    configStart();

    m_lineMarkerColorSet[index] = true;
    m_lineMarkerColor[index] = col;

    configEnd();
}

const QColor &KateRendererConfig::highlightedBracketColor() const
{
    if (m_highlightedBracketColorSet || isGlobal()) {
        return m_highlightedBracketColor;
    }

    return s_global->highlightedBracketColor();
}

void KateRendererConfig::setHighlightedBracketColor(const QColor &col)
{
    if (m_highlightedBracketColorSet && m_highlightedBracketColor == col) {
        return;
    }

    configStart();

    m_highlightedBracketColorSet = true;
    m_highlightedBracketColor = col;

    configEnd();
}

const QColor &KateRendererConfig::wordWrapMarkerColor() const
{
    if (m_wordWrapMarkerColorSet || isGlobal()) {
        return m_wordWrapMarkerColor;
    }

    return s_global->wordWrapMarkerColor();
}

void KateRendererConfig::setWordWrapMarkerColor(const QColor &col)
{
    if (m_wordWrapMarkerColorSet && m_wordWrapMarkerColor == col) {
        return;
    }

    configStart();

    m_wordWrapMarkerColorSet = true;
    m_wordWrapMarkerColor = col;

    configEnd();
}

const QColor &KateRendererConfig::tabMarkerColor() const
{
    if (m_tabMarkerColorSet || isGlobal()) {
        return m_tabMarkerColor;
    }

    return s_global->tabMarkerColor();
}

void KateRendererConfig::setTabMarkerColor(const QColor &col)
{
    if (m_tabMarkerColorSet && m_tabMarkerColor == col) {
        return;
    }

    configStart();

    m_tabMarkerColorSet = true;
    m_tabMarkerColor = col;

    configEnd();
}

const QColor &KateRendererConfig::indentationLineColor() const
{
    if (m_indentationLineColorSet || isGlobal()) {
        return m_indentationLineColor;
    }

    return s_global->indentationLineColor();
}

void KateRendererConfig::setIndentationLineColor(const QColor &col)
{
    if (m_indentationLineColorSet && m_indentationLineColor == col) {
        return;
    }

    configStart();

    m_indentationLineColorSet = true;
    m_indentationLineColor = col;

    configEnd();
}

const QColor &KateRendererConfig::iconBarColor() const
{
    if (m_iconBarColorSet || isGlobal()) {
        return m_iconBarColor;
    }

    return s_global->iconBarColor();
}

void KateRendererConfig::setIconBarColor(const QColor &col)
{
    if (m_iconBarColorSet && m_iconBarColor == col) {
        return;
    }

    configStart();

    m_iconBarColorSet = true;
    m_iconBarColor = col;

    configEnd();
}

const QColor &KateRendererConfig::foldingColor() const
{
    if (m_foldingColorSet || isGlobal()) {
        return m_foldingColor;
    }

    return s_global->foldingColor();
}

void KateRendererConfig::setFoldingColor(const QColor &col)
{
    if (m_foldingColorSet && m_foldingColor == col) {
        return;
    }

    configStart();

    m_foldingColorSet = true;
    m_foldingColor = col;

    configEnd();
}

const QColor &KateRendererConfig::templateBackgroundColor() const
{
    if (m_templateColorsSet || isGlobal()) {
        return m_templateBackgroundColor;
    }

    return s_global->templateBackgroundColor();
}

const QColor &KateRendererConfig::templateEditablePlaceholderColor() const
{
    if (m_templateColorsSet || isGlobal()) {
        return m_templateEditablePlaceholderColor;
    }

    return s_global->templateEditablePlaceholderColor();
}

const QColor &KateRendererConfig::templateFocusedEditablePlaceholderColor() const
{
    if (m_templateColorsSet || isGlobal()) {
        return m_templateFocusedEditablePlaceholderColor;
    }

    return s_global->templateFocusedEditablePlaceholderColor();
}

const QColor &KateRendererConfig::templateNotEditablePlaceholderColor() const
{
    if (m_templateColorsSet || isGlobal()) {
        return m_templateNotEditablePlaceholderColor;
    }

    return s_global->templateNotEditablePlaceholderColor();
}

const QColor &KateRendererConfig::lineNumberColor() const
{
    if (m_lineNumberColorSet || isGlobal()) {
        return m_lineNumberColor;
    }

    return s_global->lineNumberColor();
}

void KateRendererConfig::setLineNumberColor(const QColor &col)
{
    if (m_lineNumberColorSet && m_lineNumberColor == col) {
        return;
    }

    configStart();

    m_lineNumberColorSet = true;
    m_lineNumberColor = col;

    configEnd();
}

const QColor &KateRendererConfig::currentLineNumberColor() const
{
    if (m_currentLineNumberColorSet || isGlobal()) {
        return m_currentLineNumberColor;
    }

    return s_global->currentLineNumberColor();
}

void KateRendererConfig::setCurrentLineNumberColor(const QColor &col)
{
    if (m_currentLineNumberColorSet && m_currentLineNumberColor == col) {
        return;
    }

    configStart();

    m_currentLineNumberColorSet = true;
    m_currentLineNumberColor = col;

    configEnd();
}

const QColor &KateRendererConfig::separatorColor() const
{
    if (m_separatorColorSet || isGlobal()) {
        return m_separatorColor;
    }

    return s_global->separatorColor();
}

void KateRendererConfig::setSeparatorColor(const QColor &col)
{
    if (m_separatorColorSet && m_separatorColor == col) {
        return;
    }

    configStart();

    m_separatorColorSet = true;
    m_separatorColor = col;

    configEnd();
}

const QColor &KateRendererConfig::spellingMistakeLineColor() const
{
    if (m_spellingMistakeLineColorSet || isGlobal()) {
        return m_spellingMistakeLineColor;
    }

    return s_global->spellingMistakeLineColor();
}

void KateRendererConfig::setSpellingMistakeLineColor(const QColor &col)
{
    if (m_spellingMistakeLineColorSet && m_spellingMistakeLineColor == col) {
        return;
    }

    configStart();

    m_spellingMistakeLineColorSet = true;
    m_spellingMistakeLineColor = col;

    configEnd();
}

const QColor &KateRendererConfig::modifiedLineColor() const
{
    if (m_modifiedLineColorSet || isGlobal()) {
        return m_modifiedLineColor;
    }

    return s_global->modifiedLineColor();
}

void KateRendererConfig::setModifiedLineColor(const QColor &col)
{
    if (m_modifiedLineColorSet && m_modifiedLineColor == col) {
        return;
    }

    configStart();

    m_modifiedLineColorSet = true;
    m_modifiedLineColor = col;

    configEnd();
}

const QColor &KateRendererConfig::savedLineColor() const
{
    if (m_savedLineColorSet || isGlobal()) {
        return m_savedLineColor;
    }

    return s_global->savedLineColor();
}

void KateRendererConfig::setSavedLineColor(const QColor &col)
{
    if (m_savedLineColorSet && m_savedLineColor == col) {
        return;
    }

    configStart();

    m_savedLineColorSet = true;
    m_savedLineColor = col;

    configEnd();
}

const QColor &KateRendererConfig::searchHighlightColor() const
{
    if (m_searchHighlightColorSet || isGlobal()) {
        return m_searchHighlightColor;
    }

    return s_global->searchHighlightColor();
}

void KateRendererConfig::setSearchHighlightColor(const QColor &col)
{
    if (m_searchHighlightColorSet && m_searchHighlightColor == col) {
        return;
    }

    configStart();

    m_searchHighlightColorSet = true;
    m_searchHighlightColor = col;

    configEnd();
}

const QColor &KateRendererConfig::replaceHighlightColor() const
{
    if (m_replaceHighlightColorSet || isGlobal()) {
        return m_replaceHighlightColor;
    }

    return s_global->replaceHighlightColor();
}

void KateRendererConfig::setReplaceHighlightColor(const QColor &col)
{
    if (m_replaceHighlightColorSet && m_replaceHighlightColor == col) {
        return;
    }

    configStart();

    m_replaceHighlightColorSet = true;
    m_replaceHighlightColor = col;

    configEnd();
}

bool KateRendererConfig::showIndentationLines() const
{
    if (m_showIndentationLinesSet || isGlobal()) {
        return m_showIndentationLines;
    }

    return s_global->showIndentationLines();
}

void KateRendererConfig::setShowIndentationLines(bool on)
{
    if (m_showIndentationLinesSet && m_showIndentationLines == on) {
        return;
    }

    configStart();

    m_showIndentationLinesSet = true;
    m_showIndentationLines = on;

    configEnd();
}

bool KateRendererConfig::showWholeBracketExpression() const
{
    if (m_showWholeBracketExpressionSet || isGlobal()) {
        return m_showWholeBracketExpression;
    }

    return s_global->showWholeBracketExpression();
}

void KateRendererConfig::setShowWholeBracketExpression(bool on)
{
    if (m_showWholeBracketExpressionSet && m_showWholeBracketExpression == on) {
        return;
    }

    configStart();

    m_showWholeBracketExpressionSet = true;
    m_showWholeBracketExpression = on;

    configEnd();
}

bool KateRendererConfig::animateBracketMatching() const
{
    return s_global->m_animateBracketMatching;
}

void KateRendererConfig::setAnimateBracketMatching(bool on)
{
    if (!isGlobal()) {
        s_global->setAnimateBracketMatching(on);
    } else if (on != m_animateBracketMatching) {
        configStart();
        m_animateBracketMatching = on;
        configEnd();
    }
}

//END

