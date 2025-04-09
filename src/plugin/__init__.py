#!/usr/bin/python
# -*- coding: utf-8 -*-
from os import environ
import gettext

from Components.Language import language
from Tools.Directories import resolveFilename, SCOPE_PLUGINS


def localeInit():
	environ["LANGUAGE"] = language.getLanguage()[:2]
	gettext.bindtextdomain("ServiceApp", resolveFilename(SCOPE_PLUGINS,
		"SystemPlugins/ServiceApp/locale"))


def _(txt):
	t = gettext.dgettext("ServiceApp", txt)
	if t == txt:
		t = gettext.gettext(txt)
	return t


localeInit()
language.addCallback(localeInit)
