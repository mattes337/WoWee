// Reading a SimpleHTML frame's text as the small HTML document it usually is.
//
// The fault this covers was visible from across the room: the login screen's
// dialogs drew their own markup, so the player was shown
//
//     <html><body><p align="CENTER">This system will not be supported ...
//
// tags and all. Nothing parsed the HTML; the text went straight to a renderer
// that knows WoW's |c escapes and nothing else.
//
// Worth testing at this level because the parser's output is a handful of
// strings and every mistake in it is silent: an unrecognised tag that eats its
// contents loses a sentence, a paragraph that does not close merges two blocks
// into one, and both still draw. No font and no device are needed - the blocks
// come out in WoW's own escape language and the drawing side is already
// covered by test_text_markup.

#include "catch_amalgamated.hpp"
#include "ui/widget_tree.hpp"

#include <string>

using wowee::ui::HtmlBlock;
using wowee::ui::Widget;
using wowee::ui::htmlBlockFont;
using wowee::ui::parseSimpleHtml;

TEST_CASE("text that is not a document comes back whole") {
    // ItemTextFrame's page - a letter, a sign, a plaque. Prose with line
    // breaks, and reading it as markup would lose the rest of any sentence
    // with a less-than sign in it.
    const std::string page = "Dearest Mother,\n\nI am well. 3 < 4 and always was.";
    const auto blocks = parseSimpleHtml(page);
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].kind == HtmlBlock::Kind::Paragraph);
    CHECK(blocks[0].text == page);
    CHECK(blocks[0].align.empty());
}

TEST_CASE("an empty string produces no blocks at all") {
    CHECK(parseSimpleHtml("").empty());
    CHECK(parseSimpleHtml("<html><body></body></html>").empty());
}

TEST_CASE("a paragraph keeps its align and loses its tags") {
    // LOGIN_BANNED, shortened. The align attribute is what centres every login
    // notice; without it they would be drawn against the frame's own justifyH.
    const auto blocks = parseSimpleHtml(
        "<html><body><p align=\"CENTER\">This account has been closed."
        "</p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "This account has been closed.");
    CHECK(blocks[0].align == "CENTER");
}

TEST_CASE("align is reported in upper case however it was written") {
    const auto blocks = parseSimpleHtml("<html><body><p align='left'>x</p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].align == "LEFT");
}

TEST_CASE("blocks stack, and each heading keeps its own kind") {
    const auto blocks = parseSimpleHtml(
        "<html><body>"
        "<h1>Terms of Use</h1>"
        "<p>First paragraph.</p>"
        "<h2>Section</h2>"
        "<h3>Subsection</h3>"
        "<p align=\"RIGHT\">Last.</p>"
        "</body></html>");
    REQUIRE(blocks.size() == 5);
    CHECK(blocks[0].kind == HtmlBlock::Kind::Heading1);
    CHECK(blocks[0].text == "Terms of Use");
    CHECK(blocks[1].kind == HtmlBlock::Kind::Paragraph);
    CHECK(blocks[2].kind == HtmlBlock::Kind::Heading2);
    CHECK(blocks[3].kind == HtmlBlock::Kind::Heading3);
    CHECK(blocks[4].align == "RIGHT");
    CHECK(blocks[4].text == "Last.");
}

TEST_CASE("text outside any block element still renders") {
    const auto blocks = parseSimpleHtml(
        "<html><body>Loose words<p>and a paragraph</p></body></html>");
    REQUIRE(blocks.size() == 2);
    CHECK(blocks[0].text == "Loose words");
    CHECK(blocks[1].text == "and a paragraph");
}

TEST_CASE("a tag nobody models is dropped and its contents kept") {
    // The forgiving reading: an unknown element that swallowed what it wrapped
    // would silently lose a sentence, and nothing on screen would say why.
    const auto blocks = parseSimpleHtml(
        "<html><body><p>plain <b>bold</b> words</p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "plain bold words");
}

TEST_CASE("a break is a newline, which the wrapper already honours") {
    const auto blocks = parseSimpleHtml("<html><body><p>one<br/>two</p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "one\ntwo");
}

TEST_CASE("entities decode") {
    const auto blocks = parseSimpleHtml(
        "<html><body><p>&lt;tag&gt; &amp; &quot;quoted&quot;</p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "<tag> & \"quoted\"");
}

TEST_CASE("a lone ampersand is left as it stands") {
    // Prose is full of them. Eating one because a semicolon turns up later in
    // the sentence would be worse than not decoding at all, so the search for
    // the closing semicolon is bounded and anything past it is only text.
    const auto plain = parseSimpleHtml("<html><body><p>you &amp me</p></body></html>");
    REQUIRE(plain.size() == 1);
    CHECK(plain[0].text == "you &amp me");
    const auto distant = parseSimpleHtml(
        "<html><body><p>Ready &amp willing; able</p></body></html>");
    REQUIRE(distant.size() == 1);
    CHECK(distant[0].text == "Ready &amp willing; able");
}

TEST_CASE("an entity this does not know is left written out") {
    const auto blocks = parseSimpleHtml("<html><body><p>a &copy; b</p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "a &copy; b");
}

TEST_CASE("a decoded entity is not re-read as a tag") {
    const auto blocks = parseSimpleHtml(
        "<html><body><p>&lt;p&gt;is not a paragraph</p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "<p>is not a paragraph");
}

TEST_CASE("an anchor becomes a hyperlink escape the renderer already draws") {
    // LOGIN_ACCOUNT_LOCKED writes its address this way. The default format is
    // WoW's own: the href is the payload and the anchor text is what is drawn.
    const auto blocks = parseSimpleHtml(
        "<html><body><p>Visit <a href=\"http://example.com/x\">example.com</a>"
        " for more.</p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text ==
          "Visit |Hhttp://example.com/x|hexample.com|h for more.");
}

TEST_CASE("an anchor written with single quotes reads the same") {
    const auto blocks = parseSimpleHtml(
        "<html><body><p><a href='http://example.com/'>here</a></p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "|Hhttp://example.com/|hhere|h");
}

TEST_CASE("the frame's hyperlink format is what wraps a link") {
    // GlueDialogHTML declares hyperlinkFormat="|cff06ff07|H%s|h[%s]|h|r", so
    // its links are green and bracketed. The href comes first and the anchor
    // text second, which is the order the format states them in.
    const auto blocks = parseSimpleHtml(
        "<html><body><p><a href=\"u\">t</a></p></body></html>",
        "|cff06ff07|H%s|h[%s]|h|r");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "|cff06ff07|Hu|h[t]|h|r");
}

TEST_CASE("an anchor with no href is only its text") {
    const auto blocks = parseSimpleHtml("<html><body><p><a>bare</a></p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "bare");
}

TEST_CASE("an ampersand inside an href decodes before it becomes a payload") {
    // LOGIN_AUTH_OUTAGE's support address is written with &amp; in it, and a
    // link that kept the entity would open a different page than the one the
    // interface named.
    const auto blocks = parseSimpleHtml(
        "<html><body><p><a href=\"http://s/?a=1&amp;b=2\">support</a></p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "|Hhttp://s/?a=1&b=2|hsupport|h");
}

TEST_CASE("an image becomes an inline texture escape") {
    const auto blocks = parseSimpleHtml(
        "<html><body><p><img src=\"Interface\\Glues\\Common\\Glue-Panel\" "
        "width=\"32\" height=\"16\"/></p></body></html>");
    REQUIRE(blocks.size() == 1);
    // |Tpath:height:width|t - the escape states the height first.
    CHECK(blocks[0].text == "|TInterface\\Glues\\Common\\Glue-Panel:16:32|t");
}

TEST_CASE("an image with no size asks for the height of its line") {
    const auto blocks = parseSimpleHtml("<html><body><p><img src=\"a\"/></p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "|Ta:0:0|t");
}

TEST_CASE("whitespace collapses and the block is trimmed") {
    // A document written across several indented lines of a file must not draw
    // its indentation.
    const auto blocks = parseSimpleHtml(
        "<html>\n  <body>\n    <p>\n      two   words\n    </p>\n  </body>\n</html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "two words");
}

TEST_CASE("WoW's own escapes pass through untouched") {
    // LOGIN_ACCOUNT_LOCKED puts |n line breaks inside its paragraph, and the
    // markup parser downstream is what turns those into breaks.
    const auto blocks = parseSimpleHtml(
        "<html><body><p align=\"CENTER\">locked.|nVisit us.</p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "locked.|nVisit us.");
}

TEST_CASE("an unclosed paragraph still ends at the end of the document") {
    const auto blocks = parseSimpleHtml("<html><body><p>dangling");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "dangling");
}

TEST_CASE("a stray less-than inside a document is a character, not a tag") {
    const auto blocks = parseSimpleHtml("<html><body><p>3 < 4</p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].text == "3 < 4");
}

TEST_CASE("upper case tags read the same as lower") {
    const auto blocks = parseSimpleHtml(
        "<HTML><BODY><P ALIGN=\"CENTER\">shout</P></BODY></HTML>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].align == "CENTER");
    CHECK(blocks[0].text == "shout");
}

TEST_CASE("a heading with no font of its own draws in the body's") {
    Widget w;
    w.fontFace = "Fonts\\FRIZQT__.TTF";
    w.fontHeight = 12.0f;
    w.lineSpacing = 2.0f;
    const auto body = htmlBlockFont(w, HtmlBlock::Kind::Paragraph);
    CHECK(body.face == "Fonts\\FRIZQT__.TTF");
    CHECK(body.height == 12.0f);
    CHECK(body.spacing == 2.0f);
    const auto h1 = htmlBlockFont(w, HtmlBlock::Kind::Heading1);
    CHECK(h1.face == "Fonts\\FRIZQT__.TTF");
    CHECK(h1.height == 12.0f);
}

TEST_CASE("a heading that was given a font uses it") {
    // What GlueDialog.xml declares: <FontStringHeader1
    // inherits="GlueFontNormalLarge" spacing="4"/>, which the emitter writes
    // out as SetFontObject("h1", ...) and SetSpacing("h1", 4).
    Widget w;
    w.fontFace = "Fonts\\FRIZQT__.TTF";
    w.fontHeight = 12.0f;
    w.htmlFonts[0].set = true;
    w.htmlFonts[0].face = "Fonts\\MORPHEUS.TTF";
    w.htmlFonts[0].height = 16.0f;
    w.htmlFonts[0].spacing = 4.0f;
    w.htmlFonts[0].color[0] = 1.0f;
    w.htmlFonts[0].color[1] = 0.82f;
    w.htmlFonts[0].color[2] = 0.0f;
    const auto h1 = htmlBlockFont(w, HtmlBlock::Kind::Heading1);
    CHECK(h1.face == "Fonts\\MORPHEUS.TTF");
    CHECK(h1.height == 16.0f);
    CHECK(h1.spacing == 4.0f);
    CHECK(h1.color[1] == Catch::Approx(0.82f));
    // The other two headings were never told anything and stay with the body.
    CHECK(htmlBlockFont(w, HtmlBlock::Kind::Heading2).face == "Fonts\\FRIZQT__.TTF");
}

TEST_CASE("spacing set on a heading without a font still applies") {
    // SetSpacing is its own call, so a frame may state the gap and inherit the
    // type. Reading the spacing only when a font object had been set would
    // drop it.
    Widget w;
    w.fontHeight = 12.0f;
    w.lineSpacing = 2.0f;
    w.htmlFonts[1].spacing = 6.0f;
    CHECK(htmlBlockFont(w, HtmlBlock::Kind::Heading2).spacing == 6.0f);
    CHECK(htmlBlockFont(w, HtmlBlock::Kind::Heading3).spacing == 2.0f);
}

TEST_CASE("the login screen's own notice parses into one centred paragraph") {
    // Verbatim from GlueStrings.lua, which is the string that was drawn tags
    // and all.
    const auto blocks = parseSimpleHtml(
        "<html><body><p align=\"CENTER\">This World of Warcraft account has "
        "been closed and is no longer available for use.  Please go to "
        "<a href=\"http://www.worldofwarcraft.com/misc/banned.html\">"
        "http://www.worldofwarcraft.com/misc/banned.html</a> for further "
        "information.</p></body></html>");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].align == "CENTER");
    CHECK(blocks[0].text.find('<') == std::string::npos);
    CHECK(blocks[0].text.find("banned.html|h for further information.") !=
          std::string::npos);
}
