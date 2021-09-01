#include <iostream>
#include <poppler-rectangle.h>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>

#include <poppler-document.h>
#include <poppler-embedded-file.h>
#include <poppler-global.h>
#include <poppler-image.h>
#include <poppler-page-renderer.h>
#include <poppler-page.h>
#include <poppler-toc.h>

using namespace std;

template <> struct fmt::formatter<poppler::rectf> : formatter<string_view> {
  template <typename FormatContext>
  auto format(const poppler::rectf &rect, FormatContext &ctx) -> decltype(ctx.out()) {
    return format_to(ctx.out(), "({},{}; {},{})", rect.left(), rect.top(), rect.right(),
                     rect.bottom());
  }
};

template <> struct fmt::formatter<poppler::text_box> : formatter<string_view> {
  template <typename FormatContext>
  auto format(const poppler::text_box &tb, FormatContext &ctx) -> decltype(ctx.out()) {
    return format_to(ctx.out(), "{}", tb.text().to_latin1());
  }
};

void error(const char *msg) {
  fmt::print("{}\n", msg);
  exit(1);
}

poppler::document *get_doc(const char *fileName) {
  const std::vector<std::string> formats = poppler::image::supported_image_formats();
  fmt::print("Supported image formats:\n");
  for (const std::string &format : formats) {
    fmt::print("    {}\n", format);
  }

  auto doc = poppler::document::load_from_file(fileName);
  if (!doc) {
    error("loading error");
  }
  if (doc->is_locked()) {
    error("encrypted document");
  }

  return doc;
}

void print_doc_info(poppler::document *doc) {
  fmt::print("{}, {} pages\n", doc->get_title().to_latin1(), doc->pages());
  fmt::print("{}", doc->metadata().to_latin1());
  fmt::print("{}", doc->page_layout());
  fmt::print("{}", doc->page_mode());
  for (auto f : doc->embedded_files()) {
    fmt::print("{}, {}, {}\n{}\n\n", f->name(), f->mime_type(), f->size(),
               f->description().to_latin1());
  }

  for (auto f : doc->fonts()) {
    fmt::print("{}, {}", f.name(), f.type());
    if (f.is_embedded()) {
      fmt::print("\n");
    } else {
      fmt::print(", {}\n", f.file());
    }
  }
}

void print_toc(poppler::toc_item *root) {
  fmt::print("{}\n", root->title().to_latin1());
  for (auto child : root->children()) {
    print_toc(child);
  }
}

void _page_text(poppler::page *page) {
  // This doesn't properly render bottom copyright notice
  /* fmt::print("{}", page->text().to_latin1()); */

  // Seems to be same as previous
  fmt::print("--------------------------------\n");
  fmt::print("{}", page->text(poppler::rectf(), poppler::page::physical_layout).to_latin1());

  // Not the same but similar
  fmt::print("--------------------------------\n");
  fmt::print("{}", page->text(poppler::rectf(), poppler::page::raw_order_layout).to_latin1());

  // Same as prev
  fmt::print("--------------------------------\n");
  fmt::print("{}",
             page->text(poppler::rectf(), poppler::page::non_raw_non_physical_layout).to_latin1());
}

void _page_text_rect(poppler::page *page) {
  // Only text from rect is shown, improperly like usual
  auto rect = page->page_rect();
  fmt::print("{}\n", rect);
  rect.set_top(700);
  fmt::print("{}\n", rect);
  fmt::print("{}\n", page->text(rect, poppler::page::non_raw_non_physical_layout).to_latin1());
}

void _page_text_boxes(poppler::page *page) {
  auto boxes = page->text_list();
  for (const auto &box : boxes) {
    fmt::print("{}\n", box);
  }
}

void print_pages(poppler::document *doc) {
  auto num = doc->pages();
  auto rect = poppler::rectf(0, 0, 612, 698);

  for (int i = 0; i < 9; i++) {
    auto page = doc->create_page(i);
    fmt::print("{}.\n", i);

    fmt::print("{}", page->text(rect, poppler::page::raw_order_layout).to_latin1());

    fmt::print("--------------------------------\n");
  }
}

const char *fileName = "mankiw_1-100.pdf";

int main(int argc, char **argv) {
  /* fmt::print("{}","Hello world!"); */
  if (argc > 1) {
    fileName = argv[1];
  }

  auto doc = get_doc(fileName);
  /* print_doc_info(doc); */
  /* print_toc(doc->create_toc()->root()); */
  print_pages(doc);

  return 0;
}
