from gettext import bindtextdomain, dgettext, dngettext, gettext, ngettext

from Components.Language import language
from Tools.Directories import resolveFilename, SCOPE_PLUGINS


PluginLanguageDomain = "ServiceApp"


def localeInit():
	bindtextdomain(PluginLanguageDomain, resolveFilename(SCOPE_PLUGINS, "SystemPlugins/ServiceApp/locale"))


def _(txt):
	t = dgettext(PluginLanguageDomain, txt)
	if t == txt:
		t = gettext(txt)
	return t


def _ngettext(singular, plural, n):
	trans = dngettext(PluginLanguageDomain, singular, plural, n)
	if trans in (singular, plural):
		trans = ngettext(singular, plural, n)
	return trans


localeInit()
language.addCallback(localeInit)
