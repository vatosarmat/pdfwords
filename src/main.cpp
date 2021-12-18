#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <unordered_map>
#include <vector>

#include <boost/container_hash/hash.hpp>
#include <boost/optional.hpp>
#include <boost/optional/optional_io.hpp>
#include <boost/program_options.hpp>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <poppler-document.h>
#include <poppler-embedded-file.h>
#include <poppler-global.h>
#include <poppler-page-renderer.h>
#include <poppler-page.h>
#include <poppler-toc.h>

#include "utils.h"

using namespace std;
using poppler::rectf;
using poppler::ustring;
namespace po = boost::program_options;
template <class T>
using umap = unordered_map<ustring, T, boost::hash<ustring>>;
using uint = unsigned int;

namespace WordParser {
static constexpr auto bit(int n) -> uint32_t {
  return 1 << n;
}
enum class State : uint32_t {
  BLANK = 0,
  WORD = bit(0),
  HYPHEN = bit(1),
  APOSTROPHE = bit(2)
};

auto operator|(State a, State b) -> State {
  return static_cast<State>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
auto operator^(State a, State b) {
  return static_cast<State>(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b));
}
auto operator&(State a, State b) {
  return static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
}
auto operator^=(State& a, State b) -> State& {
  a = a ^ b;
  return a;
}
auto operator|=(State& a, State b) -> State& {
  a = a | b;
  return a;
}

auto isapostrophe(int c) -> bool {
  const array<unsigned short, 2> apostrophes = {'\'', u'’'};
  return c == apostrophes[0] || c == apostrophes[1];
}

auto ishyphen(int c) -> bool {
  return c == '-';
}

auto is_roman_numeral(const ustring& str) -> bool {
  const auto roman_numeral_regex =
      wregex(L"^m{0,4}(cm|cd|d?c{0,3})(xc|xl|l?x{0,3})(ix|iv|v?i{0,3})$");

  if (str.empty()) {
    return false;
  };
  return regex_match(wstring{str.begin(), str.end()}, roman_numeral_regex);
}

}; // namespace WordParser

class WordCounter {

  void collect_word_if_suitable(ustring&& word) {
    if (word.size() > 2) {
      ustring lowered;
      transform(word.cbegin(), word.cend(), back_inserter(lowered),
                [](unsigned short c) -> unsigned short { return tolower(c); });

      if (!filter.contains(lowered) && !WordParser::is_roman_numeral(lowered)) {
        ++word_count[move(lowered)];
      }
    }
    word.clear();
  }

  // Word:count
  umap<uint> word_count;
  umap<string> word_supplement;
  const set<ustring>& filter;

public:
  explicit WordCounter(const set<ustring>& filter, umap<uint>&& initial_count,
                       umap<string>&& supplement)
      : filter(filter), word_count(initial_count), word_supplement(supplement) {}

  void count_words(const vector<poppler::page*>& pages, boptional<int> x, boptional<int> y,
                   boptional<int> width, boptional<int> height, boptional<string> textFileName) {
    using namespace WordParser;

    boptional<ofstream> textFile;
    if (textFileName.has_value()) {
      textFile = ofstream(textFileName.value());
    }

    auto state = State::BLANK;
    ustring word;
    for (const auto& page : pages) {
      auto page_rect = page->page_rect();
      auto text =
          page->text(rectf(x.value_or(page_rect.x()), y.value_or(page_rect.y()),
                           width.value_or(page_rect.width()), height.value_or(page_rect.height())),
                     poppler::page::non_raw_non_physical_layout);

      if (textFile.has_value()) {
        textFile.value() << text.to_utf8().data() << endl;
      }
      auto it = text.begin();
      while (it != text.end()) {
        auto ch = *it++;

        if (iswalpha(ch)) {
          state |= State::WORD;
          if (state & State::HYPHEN) {
            // assume this is a word with structural hyphen
            word += '-';
            state ^= State::HYPHEN;
          }
          if (state & State::APOSTROPHE) {
            // assume this is a word with structural apostrophe
            word += '\'';
            state ^= State::APOSTROPHE;
          }
          word += ch;
        } else if (ishyphen(ch) && (state & State::WORD)) {
          // consider hyphen only if alpha appears after it
          state |= State::HYPHEN;
        } else if (isapostrophe(ch) && (state & State::WORD)) {
          // consider apostrophe only if alpha appears after it
          state |= State::APOSTROPHE;
        } else if (ch == '\n' && (state & State::HYPHEN)) {
          // assume this is a hyphenated word
          state ^= State::HYPHEN;
        } else if (state & State::WORD) {
          collect_word_if_suitable(move(word));
          // this may be space, tab, punctuation, digit.
          // Sometimes PDF parser fails and control-chars garbage appears
          state = State::BLANK;
        }
      }
      // last word
      if (state & State::WORD && !(state & State::HYPHEN) && !(state & State::APOSTROPHE)) {
        collect_word_if_suitable(move(word));
      }
    }
  }

  void print() {
    multimap<unsigned int, ustring, greater<>> sorted;
    transform(word_count.cbegin(), word_count.cend(), inserter(sorted, sorted.end()),
              [](const pair<ustring, unsigned int>& item) -> pair<unsigned int, ustring> {
                return pair{item.second, item.first};
              });
    for (const auto& [count, word] : sorted) {
      auto utf8 = word.to_utf8();
      auto supplement = word_supplement.find(word);
      if (supplement != word_supplement.end()) {
        fmt::print("{:20} {:<3} {}\n", utf8.data(), count, supplement->second);
      } else {
        fmt::print("{:20} {:<3}\n", utf8.data(), count);
      }
    }
  }
};

auto load_doc_pages(const string& fileName, boptional<int> startPage, boptional<int> pagesCount)
    -> vector<poppler::page*> {
  auto* doc = poppler::document::load_from_file(resolve_path(fileName));
  if (!doc) {
    panic("loading error");
  }
  if (doc->is_locked()) {
    panic("encrypted document");
  }

  vector<poppler::page*> pages;
  int start = startPage.value_or(0);
  int end = start + pagesCount.value_or(doc->pages());
  for (int i = start; i < end; i++) {
    pages.push_back(doc->create_page(i));
  }

  return pages;
}

set<ustring> load_filter(const boptional<string>& fileName) {
  if (!fileName.has_value()) {
    return {};
  }

  ifstream ifs(resolve_path(fileName.value()).c_str());
  if (!ifs) {
    panic("Failed to open filter file {}", fileName);
  }
  set<ustring> ret;
  string utf8Line;
  while (getline(ifs, utf8Line)) {
    ret.insert(ustring::from_utf8(utf8Line.data()));
  }

  return ret;
}

pair<umap<uint>, umap<string>> load_merge_file(const boptional<string>& fileName, bool keepCount) {
  if (!fileName.has_value()) {
    return make_pair(umap<uint>{}, umap<string>{});
  }

  ifstream ifs(resolve_path(fileName.value()).c_str());
  if (!ifs) {
    panic("Failed to open filter file {}", fileName);
  }
  umap<uint> count_map;
  umap<string> supplement_map;
  string utf8Line;
  smatch mr;
  regex re{R"((\S+)\s+(\d+)\s+(.*))"};
  while (getline(ifs, utf8Line)) {
    regex_match(utf8Line, mr, re);
    auto uword = ustring::from_utf8(mr[1].str().data());
    auto count = stoul(mr[2].str());
    auto suppl = mr[3].str();
    count_map[uword] = keepCount ? count : 0;
    supplement_map[uword] = suppl;
  }

  return make_pair(count_map, supplement_map);
}

struct Config {
  string inputFile;
  boptional<string> textFile;
  boptional<string> filterFile;
  boptional<string> mergeFile;
  bool keepCount{false};
  boptional<int> x;
  boptional<int> y;
  boptional<int> width;
  boptional<int> height;
  boptional<int> startPage;
  boptional<int> pagesCount;

  void print() {
    fmt::print("Input:  {}\nFilter: {}\n{},{}\n{},{}\n", inputFile, filterFile, width, height,
               startPage, pagesCount);
  }

  static Config from_argv(int argc, const char** argv) {
    po::options_description odesc("Allowed options");
    po::positional_options_description pd;

    Config config;
    odesc.add_options()          //
        ("help", "help message") //
        ("text,T", po::value<boptional<string>>(&config.textFile),
         "text content of the input PDF file")                                               //
        ("input-file,I", po::value<string>(&config.inputFile)->required(), "input PDF file") //
        ("filter-file,F", po::value<boptional<string>>(&config.filterFile),
         "text file, list of words to be excluded from the output") //
        ("merge-file,M", po::value<boptional<string>>(&config.mergeFile),
         "text file which content is similar to the ouput to be merged with the output ") //
        ("keep-count,K", po::bool_switch(&config.keepCount),
         "keep words counts from merge-file, or reset them to 0")                       //
        ("start-page,S", po::value<boptional<int>>(&config.startPage), "start page")    //
        ("pages-count,C", po::value<boptional<int>>(&config.pagesCount), "pages count") //
        ("x,X", po::value<boptional<int>>(&config.x), "crop start x")                   //
        ("y,Y", po::value<boptional<int>>(&config.y), "crop start y")                   //
        ("width,W", po::value<boptional<int>>(&config.width), "crop width")             //
        ("height,H", po::value<boptional<int>>(&config.height), "crop height")          //
        ;
    pd.add("input-file", 1);

    po::variables_map ovm;
    try {
      po::store(po::command_line_parser(argc, argv).options(odesc).positional(pd).run(), ovm);
      po::notify(ovm);
    } catch (const boost::wrapexcept<po::required_option>& e) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      fmt::print("Usage: {} [options] {}\n", argv[0], pd.name_for_position(0));
      cout << odesc << endl;
      exit(1);
    }
    if (ovm.count("help") != 0) {
      cout << odesc << endl;
      exit(1);
    }

    return config;
  }

  void resolve_symlinks() {
    this->inputFile = resolve_path(this->inputFile);
    if (this->textFile.has_value()) {
      this->textFile = resolve_path(this->textFile.value());
    }
    if (this->filterFile.has_value()) {
      this->filterFile = resolve_path(this->filterFile.value());
    }
    if (this->mergeFile.has_value()) {
      this->mergeFile = resolve_path(this->mergeFile.value());
    }
  }
};

int main(int argc, const char** argv) {
  std::setlocale(LC_ALL, "en_US.UTF-8");
  auto config = Config::from_argv(argc, argv);
  auto pages = load_doc_pages(config.inputFile, config.startPage, config.pagesCount);
  auto filter = load_filter(config.filterFile);
  auto [count_map, suppl_map] = load_merge_file(config.mergeFile, config.keepCount);
  WordCounter wordCounter{filter, std::move(count_map), std::move(suppl_map)};
  wordCounter.count_words(pages, config.x, config.y, config.width, config.height, config.textFile);
  wordCounter.print();

  return 0;
}
