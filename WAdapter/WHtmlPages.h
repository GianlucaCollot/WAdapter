#ifndef WEB_THING_HTML_PAGES_H
#define WEB_THING_HTML_PAGES_H

#include "Arduino.h"
#include "WStringStream.h"

const char* HTTP_SELECTED PROGMEM = "selected";
const char* HTTP_CHECKED PROGMEM = "checked";
const char* HTTP_NONE PROGMEM = "none";
const char* HTTP_BLOCK PROGMEM = "block";
const char* HTTP_TRUE PROGMEM = "true";
const char* HTTP_FALSE PROGMEM = "false";

const static char HTTP_HEAD_BEGIN[]         PROGMEM = R"=====(<!DOCTYPE html>
<html lang='en'>
	<head>
		<meta charset="utf-8" />
		<meta name='viewport' content='width=device-width, initial-scale=1, user-scalable=no'/>
		<title>%s - %s</title>
)=====";

const static char HTTP_HEAD_STYLE[]              PROGMEM = "<link rel=\"stylesheet\" href=\"/css\" />";

const static char HTTP_HEAD_SCRIPT[]             PROGMEM = "<script src=\"/js\"></script>";

const static char HTTP_HEAD_END[]           PROGMEM = R"=====(
	</head>
	<body>
		<div id='bodyDiv'>
)=====";

const static char HTTP_BODY_END[]           PROGMEM = R"=====(
		</div>
	</body>
</html>
)=====";

const static char PAGE_CSS[]           PROGMEM = R"=====(
body{
	text-align: center;
	font-family: arial, sans-serif;
}
#bodyDiv{  
	display:inline-block;
	min-width:300px;
	text-align:left;
}
div{
	background-color:white;
	color:black;
	border:1.0rem;
	border-color:black;
	border-radius:0.3rem;
	background-size: 1em;
	padding:5px;
	text-align:left;
}
input[type='text'],
input[type='password'],
select,
button{
	width: 300px;
}
button{
	border:0;
	border-radius:0.3rem;
	background-color:#1fa3ec;
	color:#fff;
	line-height:2.4rem;
	font-size:1.2rem;
}
.settingstable th{
	font-weight: bold;
}
.settingstable th, .settingstable td{
	border: 1px solid black;
}
.settingstable input[type='text']{
	width: 40px;
}
)=====";

const static char PAGE_JS[]           PROGMEM = R"=====(
function eb(s) {
	return document.getElementById(s);
}
function qs(s) {
	return document.querySelector(s);
}
function sp(i) {
	eb(i).type = (eb(i).type === 'text' ? 'password' : 'text');
}
function c(l){
	eb('s').value=l.innerText||l.textContent;
	eb('p').focus();
}
function hideMqttGroup() {
	var cb = eb('mqttEnabled'); 
	var x = eb('mqttGroup');
	if (cb.checked) {
		x.style.display = 'block';
	} else {
		x.style.display = 'none';
	}
}
)=====";

const static char HTTP_BUTTON[]    PROGMEM = R"=====(
	<div>
		<form action='/%s' method='%s'>
			<button>%s</button>
		</form>
	</div>
)=====";


const static char HTTP_PAGE_CONFIGURATION_STYLE[]    PROGMEM = R"=====(
<style>
#mqttGroup {
	border: 1px solid gray;
	display:%s;
}
</style>
)=====";

const static char HTTP_PAGE_CONFIGURATION_MQTT_BEGIN[]    PROGMEM = R"=====(
	<div id='mqttGroup'>
)=====";

const static char HTTP_PAGE_CONFIGURATION_MQTT_END[]    PROGMEM = R"=====(
	</div>
)=====";

const static char HTTP_PAGE_CONFIGURATION_OPTION[]    PROGMEM = R"=====(
	<div>
		<label>
			<input type='checkbox' name='%s' value='true' %s %s>
			%s
		</label>
	</div>
)=====";

const static char HTTP_PAGE_CONFIIGURATION_OPTION_MQTTHASS[] PROGMEM = "Support Autodiscovery for Home Assistant using MQTT<br>";
const static char HTTP_PAGE_CONFIIGURATION_OPTION_MQTT[] PROGMEM = "Support MQTT";
const static char HTTP_PAGE_CONFIIGURATION_OPTION_APFALLBACK[] PROGMEM = "Enable Fallback to AP-Mode if WiFi Connection gets lost";
const static char HTTP_PAGE_CONFIIGURATION_OPTION_MQTTSINGLEVALUES[] PROGMEM = "Send all values also as single values via MQTT";

const static char HTTP_SAVED[]              PROGMEM = R"=====(
<div>
	%s
</div>
<div>
	%s
</div>
)=====";

const static char HTTP_HOME_BUTTON[]              PROGMEM = R"=====(
<div>
	<form action='/config' method='get'>
		<button>Back to configuration</button>
	</form>
</div>
)=====";

const static char HTTP_FORM_FIRMWARE[] PROGMEM = R"=====(
<form method='POST' action='' enctype='multipart/form-data'>
	<div>
		<input type='file' accept='.bin' name='update'>
	</div>
	<div>
		<button type='submit'>Update firmware</button>
	</div>
</form>
)=====";

const static char HTTP_CONFIG_PAGE_BEGIN[]         PROGMEM = R"=====(
<form method='get' action='save_%s'>
)=====";

const static char HTTP_TEXT_FIELD[]    PROGMEM = R"=====(
	<div>
		%s<br>
		<input type='text' name='%s' maxlength=%s value='%s'>
	</div>
)=====";

const static char HTTP_PASSWORD_FIELD[]    PROGMEM = R"=====(
	<div>
		<label>%s <small><input type="checkbox" onclick="sp('%s')"> (show password)</small></label><br>
		<input type='password' name='%s' id='%s' maxlength=%s value='%s'>
	</div>
)=====";

const static char HTTP_CHECKBOX[]         PROGMEM = R"=====(					
		<div>
			<label>
				<input type='checkbox' name='%s' value='true' %s>%s
			</label>
		</div>
)=====";

const static char HTTP_CHECKBOX_OPTION[]    PROGMEM = R"=====(
	<div>
		<label>
			%s<br>
			<input type='checkbox' id='%s' name='%s' value='true' %s onclick='%s'>%s
		</label>
	</div>
)=====";

const static char HTTP_COMBOBOX_BEGIN[]         PROGMEM = R"=====(
		<div>
			%s<br>
			<select name='%s'>
)=====";
const static char HTTP_COMBOBOX_ITEM[]         PROGMEM = R"=====(        		
				<option value='%s' %s>%s</option>                  
)=====";
const static char HTTP_COMBOBOX_END[]         PROGMEM = R"=====(					
			</select>
		</div>
)=====";

const static char HTTP_CONFIG_SAVE_BUTTON[]         PROGMEM = R"=====(	
		<div>
			<button type='submit'>Save configuration</button>
		</div>
</form>
)=====";


const char* STR_BYTE PROGMEM = " Byte";

void htmlTableRowTitle(AsyncResponseStream* page,  char const* title){
	page->print(F("<tr><th>"));
	page->print(title);
	page->print(F("</th><td>"));
}
void htmlTableRowTitle(AsyncResponseStream* page,  const __FlashStringHelper * title){
	page->print(F("<tr><th>"));
	page->print(title);
	page->print(F("</th><td>"));
}

void htmlTableRowEnd(AsyncResponseStream* page){
	page->print(F("</td></tr>"));
}

#endif
