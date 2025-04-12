#!/usr/bin/python
# -*- coding: utf-8 -*-
from os import environ
import gettext

from Components.Language import language
from Tools.Directories import resolveFilename, SCOPE_PLUGINS


PluginLanguageDomain = "ServiceApp"


def localeInit():
	environ["LANGUAGE"] = language.getLanguage()[:2]
	gettext.bindtextdomain(PluginLanguageDomain, resolveFilename(SCOPE_PLUGINS,
		"SystemPlugins/ServiceApp/locale"))


def _(txt):
	t = gettext.dgettext(PluginLanguageDomain, txt)
	if t == txt:
		t = gettext.gettext(txt)
	return t

def ngettext(singular, plural, n):
	if trans := gettext.dngettext(PluginLanguageDomain, singular, plural, n):
		return trans
	else:
		print("[%s] fallback to default translation for %s, %s" % (PluginLanguageDomain, singular, plural))
		return gettext.ngettext(singular, plural, n)

localeInit()
language.addCallback(localeInit)
