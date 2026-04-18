#!/usr/bin/python
# -*- coding: utf-8 -*-
from __future__ import print_function

from os.path import isfile
import json

from skin import parameters
from Components.Console import Console
from Components.config import config, ConfigSubsection, ConfigSelection, ConfigBoolean, ConfigSubDict, ConfigInteger, ConfigYesNo
from Components.Label import Label
from Components.SystemInfo import SystemInfo
from Plugins.Plugin import PluginDescriptor
from Screens.InfoBar import InfoBar, MoviePlayer
from Screens.MessageBox import MessageBox
from Screens.Screen import Screen
from Screens.Setup import Setup
from Tools.BoundFunction import boundFunction
from enigma import eEnv, eServiceReference

from . import _, ngettext
from . import serviceapp_client


SINKS_DEFAULT = ("", "")
SINKS_EXPERIMENTAL = ("dvbvideosinkexp", "dvbaudiosinkexp")

sink_choices = []
if (isfile(eEnv.resolve("$libdir/gstreamer-1.0/libgstdvbvideosink.so")) and
		isfile(eEnv.resolve("$libdir/gstreamer-1.0/libgstdvbaudiosink.so"))):
	sink_choices.append(("original", _("original")))
if (isfile(eEnv.resolve("$libdir/gstreamer-1.0/libgstdvbvideosinkexp.so")) and
		isfile(eEnv.resolve("$libdir/gstreamer-1.0/libgstdvbaudiosinkexp.so"))):
	sink_choices.append(("experimental", _("experimental")))

player_choices = [("gstplayer", _("gstplayer")), ("exteplayer3", _("exteplayer3"))]
GSTPLAYER_VERSION = None
EXTEPLAYER3_VERSION = None

config.plugins.serviceapp = ConfigSubsection()
config_serviceapp = config.plugins.serviceapp

config_serviceapp.servicemp3 = ConfigSubsection()
config_serviceapp.servicemp3.replace = ConfigBoolean(default=False, descriptions={False: _("original"), True: _("serviceapp")})
config_serviceapp.servicemp3.replace.value = serviceapp_client.isServiceMP3Replaced()
config_serviceapp.servicemp3.player = ConfigSelection(default="gstplayer", choices=player_choices)
config_serviceapp.passthrough_fix_enable = ConfigYesNo(default=False)
delay_choices = [(i, ngettext("%d millisecond", "%d milliseconds", i) % i) for i in list(range(0, 3100, 100))]  # noqa: F821
config_serviceapp.passthrough_fix_delay = ConfigSelection(choices=delay_choices, default=0)

config_serviceapp.options = ConfigSubDict()
config_serviceapp.options["servicemp3"] = ConfigSubsection()
config_serviceapp.options["servicegstplayer"] = ConfigSubsection()
config_serviceapp.options["serviceexteplayer3"] = ConfigSubsection()
for key in list(config_serviceapp.options.keys()):
	config_serviceapp.options[key].hls_explorer = ConfigYesNo(default=True)
	config_serviceapp.options[key].autoselect_stream = ConfigYesNo(default=True)
	config_serviceapp.options[key].connection_speed_kb = ConfigInteger(9999999, limits=(0, 9999999))
	config_serviceapp.options[key].autoturnon_subtitles = ConfigYesNo(default=True)

config_serviceapp.gstplayer = ConfigSubDict()
config_serviceapp.gstplayer["servicemp3"] = ConfigSubsection()
config_serviceapp.gstplayer["servicegstplayer"] = ConfigSubsection()
for key in list(config_serviceapp.gstplayer.keys()):
	config_serviceapp.gstplayer[key].sink = ConfigSelection(default="original", choices=sink_choices)
	config_serviceapp.gstplayer[key].buffer_size = ConfigInteger(8192, (1024, 1024 * 64))
	config_serviceapp.gstplayer[key].buffer_duration = ConfigInteger(0, (0, 100))
	config_serviceapp.gstplayer[key].subtitle_enabled = ConfigYesNo(default=True)

config_serviceapp.exteplayer3 = ConfigSubDict()
config_serviceapp.exteplayer3["servicemp3"] = ConfigSubsection()
config_serviceapp.exteplayer3["serviceexteplayer3"] = ConfigSubsection()
for key in list(config_serviceapp.exteplayer3.keys()):
	config_serviceapp.exteplayer3[key].aac_swdecoding = ConfigSelection(default="0", choices=[("0", _("No")), ("1", _("To AAC ADTS")), ("2", _("To AAC LATM"))])
	config_serviceapp.exteplayer3[key].eac3_swdecoding = ConfigYesNo(default=False)
	config_serviceapp.exteplayer3[key].ac3_swdecoding = ConfigYesNo(default=False)
	config_serviceapp.exteplayer3[key].dts_swdecoding = ConfigYesNo(default=False)
	config_serviceapp.exteplayer3[key].mp3_swdecoding = ConfigYesNo(default=False)
	config_serviceapp.exteplayer3[key].wma_swdecoding = ConfigYesNo(default=False)
	config_serviceapp.exteplayer3[key].lpcm_injecion = ConfigYesNo(default=False)
	config_serviceapp.exteplayer3[key].downmix = ConfigYesNo(default=False)
	config_serviceapp.exteplayer3[key].rtmp_protocol = ConfigSelection(default="auto", choices=["auto", "ffmpeg", "librtmp"])


def key_to_setting_id(key):
	setting_id = None
	if key == "servicemp3":
		setting_id = serviceapp_client.OPTIONS_SERVICEMP3
	elif key == "serviceexteplayer3":
		setting_id = serviceapp_client.OPTIONS_SERVICEEXTEPLAYER3
	elif key == "servicegstplayer":
		setting_id = serviceapp_client.OPTIONS_SERVICEGSTPLAYER
	return setting_id


def init_serviceapp_settings():
	for key in list(config_serviceapp.options.keys()):
		setting_id = key_to_setting_id(key)
		serviceapp_cfg = config_serviceapp.options[key]

		serviceapp_client.setServiceAppSettings(setting_id,
				serviceapp_cfg.hls_explorer.value,
				serviceapp_cfg.autoselect_stream.value,
				serviceapp_cfg.connection_speed_kb.value,
				serviceapp_cfg.autoturnon_subtitles.value)

	for key in list(config_serviceapp.gstplayer.keys()):
		setting_id = key_to_setting_id(key)
		player_cfg = config_serviceapp.gstplayer[key]

		if player_cfg.sink.value == "original":
			video_sink, audio_sink = SINKS_DEFAULT
		elif player_cfg.sink.value == "experimental":
			video_sink, audio_sink = SINKS_EXPERIMENTAL
		else:
			continue

		serviceapp_client.setGstreamerPlayerSettings(setting_id,
				video_sink,
				audio_sink,
				player_cfg.subtitle_enabled.value,
				player_cfg.buffer_size.value,
				player_cfg.buffer_duration.value)

	for key in list(config_serviceapp.exteplayer3.keys()):
		setting_id = key_to_setting_id(key)
		player_cfg = config_serviceapp.exteplayer3[key]

		rtmp_proto_val = 0
		rtmp_proto_cfg_val = player_cfg.rtmp_protocol.value
		if rtmp_proto_cfg_val == "librtmp":
			rtmp_proto_val = 2
		elif rtmp_proto_cfg_val == "ffmpeg":
			rtmp_proto_val = 1
		else:
			rtmp_proto_val = 0
		serviceapp_client.setExtEplayer3Settings(
			setting_id,
			int(player_cfg.aac_swdecoding.value),
			player_cfg.dts_swdecoding.value,
			player_cfg.wma_swdecoding.value,
			player_cfg.lpcm_injecion.value,
			player_cfg.downmix.value,
			player_cfg.ac3_swdecoding.value,
			player_cfg.eac3_swdecoding.value,
			player_cfg.mp3_swdecoding.value,
			rtmp_proto_val)

	if config_serviceapp.servicemp3.player.value == "gstplayer":
		serviceapp_client.setServiceMP3GstPlayer()
	elif config_serviceapp.servicemp3.player.value == "exteplayer3":
		serviceapp_client.setServiceMP3ExtEplayer3()


init_serviceapp_settings()


class ServiceAppSettings(Setup):
	def __init__(self, session):
		self.indent = parameters.get("SetupIndent", "  ")
		self.spacer = ("---",)
		Setup.__init__(self, session)
		self.title = _("ServiceApp")

	def gstplayer_options(self, gstplayer_options_cfg, config_list):
		config_list.append((self.indent + _("Sink"), gstplayer_options_cfg.sink, _("Select sink which you want to use.")))
		config_list.append((self.indent + _("Embedded subtitles"), gstplayer_options_cfg.subtitle_enabled, _("Turn on the embedded subtitles support.")))
		config_list.append((self.indent + _("Buffer size"), gstplayer_options_cfg.buffer_size, _("Set buffer size in kilobytes.")))
		config_list.append((self.indent + _("Buffer duration"), gstplayer_options_cfg.buffer_duration, _("Set buffer duration in seconds.")))

	def exteplayer3_options(self, exteplayer3_options_cfg, config_list):
		config_list.append((self.indent + _("AAC software decoding"), exteplayer3_options_cfg.aac_swdecoding, _("Turn on AAC software decoding.")))
		config_list.append((self.indent + _("EAC3 software decoding"), exteplayer3_options_cfg.eac3_swdecoding, _("Turn on EAC3 software decoding.")))
		config_list.append((self.indent + _("AC3 software decoding"), exteplayer3_options_cfg.ac3_swdecoding, _("Turn on AC3 software decoding.")))
		config_list.append((self.indent + _("DTS software decoding"), exteplayer3_options_cfg.dts_swdecoding, _("Turn on DTS software decoding.")))
		config_list.append((self.indent + _("MP3 software decoding"), exteplayer3_options_cfg.mp3_swdecoding, _("Turn on MP3 software decoding.")))
		config_list.append((self.indent + _("WMA software decoding"), exteplayer3_options_cfg.wma_swdecoding, _("Turn on WMA1, WMA2, WMA/PRO software decoding.")))
		config_list.append((self.indent + _("Stereo downmix"), exteplayer3_options_cfg.downmix, _("Turn on downmix to stereo, when software decoding is in use")))
		config_list.append((self.indent + _("LPCM injection"), exteplayer3_options_cfg.lpcm_injecion, _("Software decoder use LPCM for injection (otherwise wav PCM will be used)")))
		config_list.append((self.indent + _("RTMP protocol implementation"), exteplayer3_options_cfg.rtmp_protocol, _("Set which RTMP protocol implementation will be used for playback of RTMP streams")))

	def serviceapp_options(self, serviceapp_options_cfg, config_list):
		config_list.append((self.indent + _("Auto turn on subtitles"), serviceapp_options_cfg.autoturnon_subtitles, _("Automatically turn on subtitles if available.")))
		config_list.append((self.indent + _("HLS Explorer"), serviceapp_options_cfg.hls_explorer, _("Turn on explorer to retrieve different quality streams from HLS variant playlist and select them via subservices.")))
		config_list.append((self.indent + _("Auto select stream"), serviceapp_options_cfg.autoselect_stream, _("Turn on auto-selection of streams according to set Connection speed.")))
		config_list.append((self.indent + _("Connection speed"), serviceapp_options_cfg.connection_speed_kb, _("Set connection speed in kb/s, according to which you want to have streams auto-selected")))

	def serviceapp_passthrough_options(self, config_list):
		if SystemInfo["Vu_EAC3_fix"]:
			config_list.append((_("Enable AC3+ passthrough fix"), config_serviceapp.passthrough_fix_enable, _("Enables AC3+ passthrough fix for Vu+ Ultimo4K / Duo4KSE.")))
			if config_serviceapp.passthrough_fix_enable.value:
				config_list.append((_("AC3+ Passthrough fix delay"), config_serviceapp.passthrough_fix_delay, _("Select the delay that will be used for AC3+ Passthrough fix.")))

	def player_options(self, player_type, service_type, config_list):
		player_cfg = getattr(config_serviceapp, player_type)[service_type]
		serviceapp_cfg = config_serviceapp.options[service_type]
		if player_type == "exteplayer3":
			config_list.append((self.indent + _("ExtEplayer3"), ConfigSelection([EXTEPLAYER3_VERSION and (EXTEPLAYER3_VERSION, _("version %s") % str(EXTEPLAYER3_VERSION)) or (("not installed"), _("not installed"))])))
			if EXTEPLAYER3_VERSION:
				self.exteplayer3_options(player_cfg, config_list)
				self.serviceapp_options(serviceapp_cfg, config_list)
		if player_type == "gstplayer":
			config_list.append((self.indent + _("GstPlayer"), ConfigSelection([GSTPLAYER_VERSION and (GSTPLAYER_VERSION, _("version %s") % str(GSTPLAYER_VERSION)) or (("not installed"), _("not installed"))])))
			if GSTPLAYER_VERSION:
				self.gstplayer_options(player_cfg, config_list)
				self.serviceapp_options(serviceapp_cfg, config_list)

	def createSetup(self):
		config_list = [(_("Enigma2 playback system") + "*", config_serviceapp.servicemp3.replace, _("Select the player which will be used for Enigma2 playback."))]
		if config_serviceapp.servicemp3.replace.value:
			config_list.append((_("Player"), config_serviceapp.servicemp3.player, _("Select the player which will be used in serviceapp for Enigma2 playback.")))
			self.serviceapp_passthrough_options(config_list)
			config_list.append(self.spacer)
			config_list.append((_("ServiceMp3 (%s)" % str(serviceapp_client.ID_SERVICEMP3)),))
			if config_serviceapp.servicemp3.player.value == "gstplayer":
				self.player_options("gstplayer", "servicemp3", config_list)
			elif config_serviceapp.servicemp3.player.value == "exteplayer3":
				self.player_options("exteplayer3", "servicemp3", config_list)
		self.serviceapp_passthrough_options(config_list)
		config_list.append(self.spacer)
		config_list.append((_("ServiceGstPlayer (%s)" % str(serviceapp_client.ID_SERVICEGSTPLAYER)),))
		self.player_options("gstplayer", "servicegstplayer", config_list)
		config_list.append(self.spacer)
		config_list.append((_("ServiceExtEplayer3 (%s)" % str(serviceapp_client.ID_SERVICEEXTEPLAYER3)),))
		self.player_options("exteplayer3", "serviceexteplayer3", config_list)
		self["config"].list = config_list

	def keySave(self):
		init_serviceapp_settings()
		serviceapp_client.setServiceMP3Replace(config_serviceapp.servicemp3.replace.value)
		if self.saveAll():
			msg = _("Enigma2 playback system was changed and Enigma2 should be restarted\n\nDo you want to restart it now?")
			self.session.openWithCallback(self.close, MessageBox, msg, type=MessageBox.TYPE_YESNO)
		else:
			self.close()


class ServiceAppPlayer(MoviePlayer):
	def __init__(self, session, service):
		MoviePlayer.__init__(self, session, service)
		self.skinName = ["ServiceAppPlayer", "MoviePlayer"]
		self.servicelist = InfoBar.instance and InfoBar.instance.servicelist

	def handleLeave(self, how):
		if how == "ask":
			self.session.openWithCallback(self.leavePlayerConfirmed, MessageBox, _("Stop playing this movie?"))
		else:
			self.close()

	def leavePlayerConfirmed(self, answer):
		if answer:
			self.close()


class ServiceAppDetectPlayers(Screen):
	skin = """
		<screen position="center,center" size="500,340" title="ServiceApp - player check">
			<widget name="text" position="10,10" size="490,325" font="Regular;28" halign="center" valign="center" />
		</screen>
				"""

	def __init__(self, session):
		Screen.__init__(self, session)
		self["text"] = Label()
		self.players_iter = iter(
			[
				("gstplayer_gst-1.0", _("Detecting gstreamer player ..."), self.detect_gstplayer),
				("exteplayer3", _("Detecting exteplayer3 player ..."), self.detect_exteplayer3),
			]
		)
		self.onLayoutFinish.append(self.detect_next_player)

	def detect_next_player(self):
		player = next(self.players_iter, None)
		if player is not None:
			self["text"].setText(player[1])
			self.console = Console()
			self.console.ePopen(player[0], boundFunction(self.detect_player_cb, player[2]))
		else:
			self.close()

	def detect_player_cb(self, datafnc, data, retval, extra_args):
		datafnc(data, retval, extra_args)
		self.detect_next_player()

	def _get_first_json_data_from_string(self, data):
		jsondata = None
		for line in data.splitlines():
			try:
				jsondata = json.loads(line)
				break
			except ValueError:
				pass
		return jsondata

	def detect_gstplayer(self, data, retval, extra_args):
		global GSTPLAYER_VERSION
		GSTPLAYER_VERSION = None
		jsondata = self._get_first_json_data_from_string(data)
		if jsondata is None:
			print("[ServiceApp] cannot detect gstplayer version(1)!")
			return
		try:
			GSTPLAYER_VERSION = jsondata["GSTPLAYER_EXTENDED"]["version"]
		except KeyError:
			print("[ServiceApp] cannot detect gstplayer version(2)!")
		else:
			print("[ServiceApp] found gstplayer - %d version" % GSTPLAYER_VERSION)

	def detect_exteplayer3(self, data, retval, extra_args):
		global EXTEPLAYER3_VERSION
		EXTEPLAYER3_VERSION = None
		jsondata = self._get_first_json_data_from_string(data)
		if jsondata is None:
			print("[ServiceApp] cannot detect exteplayer3 version(1)!")
			return
		try:
			EXTEPLAYER3_VERSION = jsondata["EPLAYER3_EXTENDED"]["version"]
		except KeyError:
			print("[ServiceApp] cannot detect exteplayer3 version(2)!")
		else:
			print("[ServiceApp] found exteplayer3 - %d version" % EXTEPLAYER3_VERSION)


def main(session, **kwargs):

	def restart_enigma2(restart=False):
		if restart:
			from Screens.Standby import TryQuitMainloop
			session.open(TryQuitMainloop, 3)

	def open_serviceapp_settings(callback=None):
		session.openWithCallback(restart_enigma2, ServiceAppSettings)

	session.openWithCallback(open_serviceapp_settings, ServiceAppDetectPlayers)


def menu(menuid, **kwargs):
	if menuid == "system":
		return [(_("ServiceApp"), main, "serviceapp_setup", None)]
	return []


def play_exteplayer3(session, service, **kwargs):
	ref = eServiceReference(5002, 0, service.getPath())
	session.open(ServiceAppPlayer, service=ref)


def play_gstplayer(session, service, **kwargs):
	ref = eServiceReference(5001, 0, service.getPath())
	session.open(ServiceAppPlayer, service=ref)


def Plugins(**kwargs):
	return [
		PluginDescriptor(name=_("ServiceApp"), description=_("setup player framework"),
			where=PluginDescriptor.WHERE_MENU, needsRestart=False, fnc=menu),
		PluginDescriptor(name=_("ServiceApp"), description=_("Play with ServiceExtEplayer3"),
			where=PluginDescriptor.WHERE_MOVIELIST, needsRestart=False, fnc=play_exteplayer3),
		PluginDescriptor(name=_("ServiceApp"), description=_("Play with ServiceGstPlayer"),
			where=PluginDescriptor.WHERE_MOVIELIST, needsRestart=False, fnc=play_gstplayer)
	]
