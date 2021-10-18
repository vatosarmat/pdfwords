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

using namespace std;
template <class T>
using boptional = boost::optional<T>;
using poppler::rectf;
using poppler::ustring;
namespace po = boost::program_options;

template <typename V>
struct fmt::formatter<boptional<V>> : formatter<string_view> {
  template <typename FormatContext>
  auto format(const boptional<V>& opt, FormatContext& ctx) -> decltype(ctx.out()) {
    if (opt.has_value()) {
      return format_to(ctx.out(), "{}", opt.value());
    }
    return format_to(ctx.out(), "NONE");
  }
};

template <typename... T>
void panic(fmt::format_string<T...> fmt, T&&... args) {
  fmt::print(stderr, fmt, args...);
  std::cout << endl;
  exit(1);
}

namespace WordParser {
static constexpr uint32_t bit(int n) {
  return 1 << n;
}
enum class State : uint32_t {
  BLANK = 0,
  WORD = bit(0),
  HYPHEN = bit(1),
  APOSTROPHE = bit(2)
};

State operator|(State a, State b) {
  return static_cast<State>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
State operator^(State a, State b) {
  return static_cast<State>(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b));
}
bool operator&(State a, State b) {
  return static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
}
State& operator^=(State& a, State b) {
  a = a ^ b;
  return a;
}
State& operator|=(State& a, State b) {
  a = a | b;
  return a;
}

bool isapostrophe(int c) {
  const array<unsigned short, 2> apostrophes = {'\'', u'’'};
  return c == apostrophes[0] || c == apostrophes[1];
}

bool ishyphen(int c) {
  return c == '-';
}

const auto roman_numeral_regex =
    wregex(L"^m{0,4}(cm|cd|d?c{0,3})(xc|xl|l?x{0,3})(ix|iv|v?i{0,3})$");

bool is_roman_numeral(const ustring& str) {
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
        ++data[move(lowered)];
      }
    }
    word.clear();
  }

  // Word:count
  unordered_map<ustring, unsigned int, boost::hash<ustring>> data;
  const set<ustring>& filter;

public:
  explicit WordCounter(const set<ustring>& filter) : filter(filter), data() {}

  void count_words(const vector<poppler::page*>& pages, boptional<int> width, boptional<int> height,
                   bool log) {
    using namespace WordParser;

    auto state = State::BLANK;
    ustring word;
    for (const auto& page : pages) {
      auto page_rect = page->page_rect();
      auto text = page->text(rectf(page_rect.x(), page_rect.y(), width.value_or(page_rect.width()),
                                   height.value_or(page_rect.height())),
                             poppler::page::non_raw_non_physical_layout);
      if (log) {
        fmt::print("{}\n", text.to_utf8().data());
      }

      text.to_utf8();
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
    multimap<unsigned int, ustring, greater<unsigned int>> sorted;
    transform(data.cbegin(), data.cend(), inserter(sorted, sorted.end()),
              [](const pair<ustring, unsigned int>& item) -> pair<unsigned int, ustring> {
                return pair{item.second, item.first};
              });
    for (const auto& [count, word] : sorted) {
      fmt::print("{:20} {}\n", word.to_utf8().data(), count);
    }
  }
};

vector<poppler::page*> load_doc_pages(const string& fileName, boptional<int> startPage,
                                      boptional<int> pagesCount) {
  auto doc = poppler::document::load_from_file(fileName.c_str());
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

  ifstream ifs(fileName.value().c_str());
  if (!ifs) {
    panic("Failed to open filter file %s", fileName);
  }
  set<ustring> ret;
  string utf8Line;
  while (getline(ifs, utf8Line)) {
    ret.insert(ustring::from_utf8(utf8Line.data()));
  }

  return ret;
}

struct Config {
  string inputFile;
  bool log;
  boptional<string> filterFile;
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
    odesc.add_options()                                                                      //
        ("help", "help message")                                                             //
        ("log,L", po::bool_switch(&config.log), "print page content")                        //
        ("input-file,I", po::value<string>(&config.inputFile)->required(), "input pdf file") //
        ("filter-file,F", po::value<boptional<string>>(&config.filterFile),
         "text file, list of words to be excluded from the output")                     //
        ("start-page,S", po::value<boptional<int>>(&config.startPage), "start page")    //
        ("pages-count,C", po::value<boptional<int>>(&config.pagesCount), "pages count") //
        ("width,W", po::value<boptional<int>>(&config.width), "crop width")             //
        ("height,H", po::value<boptional<int>>(&config.height), "crop height")          //
        ;
    pd.add("input-file", 1);

    po::variables_map ovm;
    try {
      po::store(po::command_line_parser(argc, argv).options(odesc).positional(pd).run(), ovm);
      po::notify(ovm);
    } catch (const boost::wrapexcept<po::required_option>& e) {
      cout << odesc << endl;
      exit(1);
    }
    if (ovm.count("help")) {
      cout << odesc << endl;
      exit(1);
    }

    return config;
  }
};

int main(int argc, const char** argv) {
  std::setlocale(LC_ALL, "en_US.UTF-8");
  auto config = Config::from_argv(argc, argv);
  auto pages = load_doc_pages(config.inputFile, config.startPage, config.pagesCount);
  auto filter = load_filter(config.filterFile);
  WordCounter wordCounter{filter};
  wordCounter.count_words(pages, config.width, config.height, config.log);
  wordCounter.print();

  return 0;
}
