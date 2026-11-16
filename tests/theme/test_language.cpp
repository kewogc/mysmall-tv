// Localized text: a text `value` per language under spec 2 (English and
// Russian), the fallback to English, the fixed limits, Russian letters in a
// manifest and in fetched values, and month/weekday names that follow the
// device language -- for spec 2 themes only.
#include "ThemeEngine.h"
#include "Text866.h"
#include <cassert>
#include <iostream>
using namespace smalltv;

// Russian as the engine holds it: one CP866 byte per letter.
static std::string ru(const std::string& utf8) {
  std::string s = utf8;
  s.resize(utf8ToCp866(s.data(), s.size(), &s[0]));
  return s;
}
static std::string manifest(int spec, const std::string& value) {
  return "{\"spec\":" + std::to_string(spec) + ",\"theme\":{\"id\":\"t\",\"name\":\"T\",\"author\":\"A\",\"version\":\"1\"},"
    "\"display\":{\"width\":240,\"height\":240,\"background\":\"#000000\"},\"layers\":[{\"id\":\"l\",\"type\":\"text\","
    "\"x\":0,\"y\":0,\"value\":" + value + ",\"size\":16,\"color\":\"#ffffff\"}]}";
}
static bool parses(int spec, const std::string& value, Theme& t, std::string& error) {
  return parseTheme(manifest(spec, value), t, error);
}
static bool parses(int spec, const std::string& value) {
  Theme t;
  std::string error;
  return parses(spec, value, t, error);
}
static std::string rendered(const Theme& theme, uint8_t language, const tm& t) {
  Engine engine;
  engine.setTheme(theme, 0);
  engine.setLanguage(language);
  engine.update(0, &t);
  return engine.states()[0].text;
}

int main() {
  Theme theme;
  std::string error;
  tm t{};
  t.tm_year = 126; t.tm_mon = 8; t.tm_mday = 17; t.tm_wday = 4;  // Thursday 17 September 2026
  const uint8_t RU = languageFromCode("ru");
  assert(kLanguageCount == 2 && RU == 1);

  // Parse and fall back to English.
  assert(parses(2, R"({"en":"Hello {DD}","ru":"Privet {DD}"})", theme, error));
  assert(theme.spec == 2 && theme.layers[0].value == "Hello {DD}");
  assert(rendered(theme, 0, t) == "Hello 17");
  assert(rendered(theme, RU, t) == "Privet 17");
  assert(rendered(theme, languageFromCode("de"), t) == "Hello 17");  // not a language: English
  assert(rendered(theme, languageFromCode("xx"), t) == "Hello 17");
  assert(rendered(theme, 200, t) == "Hello 17");
  // An object with English alone is fine: Russian falls back to it.
  assert(parses(2, R"({"en":"Only"})", theme, error) && theme.translations.empty());
  assert(rendered(theme, RU, t) == "Only");
  // A plain string still works under spec 2, in every language.
  assert(parses(2, "\"{WEEKDAY}\"", theme, error) && theme.translations.empty());
  assert(rendered(theme, RU, t) == ru("Четверг"));

  // Russian letters. A manifest holds UTF-8; the engine holds one byte a letter,
  // so a text counts cells, and the same goes for a plain `value`.
  assert(parses(2, R"({"en":"Today is {WEEKDAY}","ru":"Сегодня {WEEKDAY}, {DD} {MONTH}"})", theme, error));
  assert(theme.translations[0].text == ru("Сегодня {WEEKDAY}, {DD} {MONTH}"));
  assert(rendered(theme, RU, t) == ru("Сегодня Четверг, 17 сентября"));
  assert(rendered(theme, RU, t).size() == 28);   // cells, not UTF-8 bytes
  assert(rendered(theme, 0, t) == "Today is Thursday");
  assert(parses(2, "\"Ёлка ёж 5°C\"", theme, error));
  assert(theme.layers[0].value == ru("Ёлка ёж 5°C") && theme.layers[0].value.size() == 11);
  for (unsigned char c : theme.layers[0].value) assert(isText866(c));
  // The 128-byte limit is on the manifest's UTF-8: 64 Russian letters fit, 65 do not.
  {
    std::string fits, over;
    for (int i = 0; i < 64; ++i) fits += "ж";
    over = fits + "ж";
    assert(parses(2, "\"" + fits + "\"", theme, error) && theme.layers[0].value.size() == 64);
    assert(!parses(2, "\"" + over + "\""));
  }
  // A spec 1 theme stays ASCII.
  assert(!parses(1, "\"Привет\""));
  // A letter the font does not have is refused, in either form of `value`.
  assert(!parses(2, "\"café\""));
  assert(!parses(2, "\"時計\""));
  assert(!parses(2, R"({"en":"a","ru":"café"})"));

  // Two localized layers keep their own texts.
  {
    const std::string json = manifest(2, R"({"en":"A","ru":"B"})");
    std::string two = json;
    two.replace(two.size() - 2, 2, R"(,{"id":"m","type":"text","x":0,"y":20,"value":{"en":"D","ru":"E"},"size":16,"color":"#ffffff"},)"
      R"({"id":"n","type":"text","x":0,"y":40,"value":"G","size":16,"color":"#ffffff"}]})");
    assert(parseTheme(two, theme, error));
    Engine engine;
    engine.setTheme(theme, 0);
    engine.setLanguage(RU);
    engine.update(0, &t);
    assert(engine.states()[0].text == "B" && engine.states()[1].text == "E" && engine.states()[2].text == "G");
    engine.setLanguage(0);
    engine.update(5, &t);
    assert(engine.states()[0].text == "A" && engine.states()[1].text == "D" && engine.states()[2].text == "G");
  }

  // Changing the language re-expands the text on the next update.
  {
    assert(parses(2, R"({"en":"One","ru":"Один"})", theme, error));
    Engine engine;
    engine.setTheme(theme, 0);
    engine.update(0, &t);
    assert(engine.states()[0].text == "One");
    engine.setLanguage(RU);
    engine.update(10, &t);
    assert(engine.states()[0].text == ru("Один"));
    engine.setLanguage(0);
    engine.update(20, &t);
    assert(engine.states()[0].text == "One");
  }

  // Spec 1 refuses an object, and ignores the device language.
  assert(!parses(1, R"({"en":"Hello"})", theme, error) && error.find("spec") != std::string::npos);
  assert(parses(1, "\"{MON} {WD}\"", theme, error));
  assert(rendered(theme, RU, t) == "Sep Thu");
  // A spec 1 theme is untouched by the translations member.
  assert(parses(1, "\"x\"", theme, error) && theme.translations.empty());

  // Validation of the object.
  assert(!parses(2, R"({})"));
  assert(!parses(2, R"({"ru":"Privet"})"));             // no English to fall back to
  assert(!parses(2, R"({"en":"a","zz":"b"})"));         // unknown language
  assert(!parses(2, R"({"en":"a","pt":"b"})", theme, error) && error.find("expected en or ru") != std::string::npos);
  assert(!parses(2, R"({"en":"a","RU":"b"})"));         // codes are lower case
  assert(!parses(2, R"({"en":"a","ru":""})"));          // empty
  assert(!parses(2, R"({"en":"a","ru":5})"));           // not a string
  assert(!parses(2, R"({"en":"a","ru":{"x":"y"}})"));   // nested
  assert(!parses(2, R"({"en":"a","ru":["b"]})"));
  assert(!parses(2, R"({"en":"a","ru":"{nope}"})"));    // undeclared variable
  assert(!parses(2, R"({"en":"a","ru":"unmatched }"})"));
  assert(!parses(2, R"({"en":"a","ru":"b\u0001"})"));
  assert(!parses(2, R"([])"));
  assert(!parses(2, R"(null)"));
  // Limits: 128 bytes per language.
  const std::string max(128, 'x'), over(129, 'x');
  assert(parses(2, "{\"en\":\"" + max + "\",\"ru\":\"" + max + "\"}"));
  assert(!parses(2, "{\"en\":\"a\",\"ru\":\"" + over + "\"}"));
  assert(!parses(2, "{\"en\":\"" + over + "\"}"));
  // Data variables are checked in every language, and a fetched value may be Russian.
  {
    const std::string json =
      R"({"spec":2,"theme":{"id":"t","name":"T","author":"A","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},)"
      R"("data":[{"id":"w","url":"https://example.test/w.json","interval":60,"insecureTls":true,"fields":[{"id":"temp","path":"t"}]}],)"
      R"("layers":[{"id":"l","type":"text","x":0,"y":0,"value":{"en":"{w.temp} C","ru":"{w.temp} град"},"size":16,"color":"#ffffff"}]})";
    assert(parseTheme(json, theme, error));
    Engine engine;
    engine.setTheme(theme, 0);
    engine.setValues({{"w.temp", "21"}});
    engine.setLanguage(RU);
    engine.update(0, &t);
    assert(engine.states()[0].text == ru("21 град"));
    engine.setValues({{"w.temp", "Ясно ☀ +5°"}});   // UTF-8 in; a sign the font lacks reads '?'
    engine.update(5, &t);
    assert(engine.states()[0].text == ru("Ясно ? +5° град"));
    std::string bad = json;
    bad.replace(bad.find("{w.temp} град"), std::string("{w.temp} град").size(), "{w.gone} град");
    assert(!parseTheme(bad, theme, error));
  }

  // Month and weekday names by language, spec 2. Russian months are genitive.
  assert(expandText("{WD} {WEEKDAY} {MON} {MONTH}", &t, {}, 0) == "Thu Thursday Sep September");
  assert(expandText("{WD} {WEEKDAY} {MON} {MONTH}", &t, {}, RU) == ru("Чт Четверг сен сентября"));
  {
    const char* months[12] = {"января", "февраля", "марта", "апреля", "мая", "июня", "июля", "августа", "сентября", "октября", "ноября", "декабря"};
    const char* days[7] = {"Воскресенье", "Понедельник", "Вторник", "Среда", "Четверг", "Пятница", "Суббота"};
    for (int i = 0; i < 12; ++i) { tm m{}; m.tm_mon = i; assert(expandText("{MONTH}", &m, {}, RU) == ru(months[i])); }
    for (int i = 0; i < 7; ++i) { tm m{}; m.tm_wday = i; assert(expandText("{WEEKDAY}", &m, {}, RU) == ru(days[i])); }
  }
  // Every name of every language: 2..13 cells, letters the font has, no overrun of its slot.
  for (uint8_t language = 0; language < kLanguageCount; ++language) {
    for (int month = 0; month < 12; ++month) {
      tm m{}; m.tm_mon = month; m.tm_wday = month % 7;
      for (const char* token : {"{MON}", "{MONTH}", "{WD}", "{WEEKDAY}"}) {
        std::string name = expandText(token, &m, {}, language);
        assert(name.size() >= 2 && name.size() <= 13);
        if (std::string(token) == "{MON}" || std::string(token) == "{WD}") assert(name.size() <= 4);   // {MON} {WD}: an abbreviation
        for (unsigned char c : name) assert(isText866(c) && c != ' ');
      }
    }
  }
  // No abbreviation is shared by two months in any language, nor by two days.
  for (uint8_t language = 0; language < kLanguageCount; ++language) {
    for (int a = 0; a < 12; ++a) for (int b = a + 1; b < 12; ++b) {
      tm x{}, y{}; x.tm_mon = a; y.tm_mon = b;
      assert(expandText("{MON}", &x, {}, language) != expandText("{MON}", &y, {}, language));
    }
    for (int a = 0; a < 7; ++a) for (int b = a + 1; b < 7; ++b) {
      tm x{}, y{}; x.tm_wday = a; y.tm_wday = b;
      assert(expandText("{WD}", &x, {}, language) != expandText("{WD}", &y, {}, language));
    }
  }
  // The stored text is returned as is, not copied: a translation or the English value.
  {
    assert(parses(2, R"({"en":"One","ru":"Odin"})", theme, error));
    assert(&layerText(theme, theme.layers[0], RU) == &theme.translations[0].text);
    assert(&layerText(theme, theme.layers[0], 0) == &theme.layers[0].value);
    assert(&layerText(theme, theme.layers[0], languageFromCode("de")) == &theme.layers[0].value);
  }
  // The translations of a theme share 1024 bytes of RAM, each costing its entry
  // and, past the inline buffer, its text: as many of 128 characters as fit
  // and then short ones fit, one more does not.
  {
    const size_t full = sizeof(LocalizedText) + 129, brief = sizeof(LocalizedText);
    const size_t big = MaxTranslationBytes / full, small = (MaxTranslationBytes - big * full) / brief;
    auto manifest = [](size_t big, size_t small) {
      std::string json = "{\"spec\":2,\"theme\":{\"id\":\"t\",\"name\":\"T\",\"author\":\"A\",\"version\":\"1\"},"
        "\"display\":{\"width\":240,\"height\":240,\"background\":\"#000000\"},\"layers\":[";
      for (size_t i = 0; i < big + small; ++i)
        json += std::string(i ? "," : "") + "{\"id\":\"a" + std::to_string(i) + "\",\"type\":\"text\",\"x\":0,\"y\":0,\"size\":16,\"color\":\"#ffffff\",\"value\":{\"en\":\"a\","
        "\"ru\":\"" + std::string(i < big ? 128 : 1, 'x') + "\"}}";
      return json + "]}";
    };
    assert(parseTheme(manifest(big, small), theme, error));
    assert(theme.translations.size() == big + small && theme.translationBytes == big * full + small * brief);
    assert(!parseTheme(manifest(big, small + 1), theme, error) && error.find("1024") != std::string::npos);
  }
  assert(expandText("{WEEKDAY}", &t, {}, 99) == "Thursday");     // out of range: English
  assert(expandText("{WEEKDAY}", &t) == "Thursday");             // the old overload: English
  assert(expandText("{HH}:{MM}", nullptr, {}, 1) == "--:--");
  assert(languageFromCode(nullptr) == 0 && languageFromCode("") == 0 && languageFromCode("ru-RU") == 0);
  assert(std::string(languageCode(1)) == "ru" && std::string(languageCode(9)) == "en");

  std::cout << "language tests passed\n";
}
