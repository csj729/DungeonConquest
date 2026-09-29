// `data/*.json`을 디스크에서 읽어 `ConfigLoader`에 물린다.
//
// **코어가 아니라 도구에 산다.** 코어는 버퍼만 받는다 (CLAUDE.md 레이어 분리) —
// Unity는 `TextAsset`, 서버는 `File.ReadAllBytes`로 각자 읽고, 파일 시스템을
// 아는 것은 이 파일뿐이다.
//
// 버퍼를 멤버로 들고 있는 이유는 **파서가 문자열을 복사하지 않기 때문**이다.
// 로드가 끝난 뒤에도 `RecipeTable`·`SimConfig`는 정수만 들고 있어 버퍼와 무관하지만,
// `load()`가 도는 동안에는 살아 있어야 한다.
#ifndef DC_DATA_FILES_H
#define DC_DATA_FILES_H

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../include/dc/config_loader.h"

namespace dc::dev {

class DataFiles {
  public:
    // `dir`은 data 디렉터리. 실패하면 false이고 `error()`가 이유를 말한다.
    bool load(const char* dir, SimConfig* cfg, RecipeTable* recipes,
              HeroBaseline* hero, ItemMeta* meta = nullptr) {
        bufs_.resize(DATA_FILE_COUNT);
        for (uint32_t i = 0; i < DATA_FILE_COUNT; ++i) {
            const DataFile f = static_cast<DataFile>(i);
            path_ = std::string(dir) + "/" + dataFileName(f);
            if (!slurp(path_.c_str(), &bufs_[i])) {
                err_ = "파일을 읽을 수 없다";
                return false;
            }
            loader_.set(f, bufs_[i].data(), bufs_[i].size());
        }
        if (!loader_.load(cfg, recipes, hero, meta)) {
            path_ = dataFileName(loader_.errorFile());
            err_  = loader_.error();
            return false;
        }
        return true;
    }

    const char*         error() const { return err_; }
    const std::string&  where() const { return path_; }
    const ConfigLoader& loader() const { return loader_; }

    // 실패 이유를 한 줄로 찍는다. 도구마다 같은 형식이라야 읽기 쉽다.
    void report() const {
        std::fprintf(stderr, "데이터 로드 실패 [%s] %s%s%s\n",
                     path_.c_str(), err_ != nullptr ? err_ : "?",
                     loader_.errorKey() != nullptr ? " · 키 " : "",
                     loader_.errorKey() != nullptr ? loader_.errorKey() : "");
    }

  private:
    static bool slurp(const char* path, std::vector<char>* out) {
        std::FILE* f = std::fopen(path, "rb");
        if (f == nullptr) return false;
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n < 0) { std::fclose(f); return false; }
        out->resize(static_cast<size_t>(n));
        const size_t got = out->empty() ? 0 : std::fread(out->data(), 1, out->size(), f);
        std::fclose(f);
        return got == out->size();
    }

    std::vector<std::vector<char>> bufs_;
    ConfigLoader                   loader_;
    std::string                    path_;
    const char*                    err_ = nullptr;
};

// 도구·테스트가 쓰는 **한 번만 읽는 접근점.**
//
// 9개 파일을 테스트마다 다시 파싱할 이유가 없고, 무엇보다 호출부가
// `dev::data().cfg`로 짧아져 설정을 들고 다니는 배관이 사라진다.
//
// **실패하면 멈춘다.** 설정 없이 도는 것보다 낫다 — 기본값으로 계속 돌면
// "왜 이렇게 약하지"를 한참 뒤에 묻게 된다.
struct LoadedData {
    SimConfig    cfg;
    RecipeTable  table;
    HeroBaseline hero;
    ItemMeta     meta;
};

inline const LoadedData& data() {
    static const LoadedData loaded = [] {
        DataFiles  files;      // 버퍼는 load()가 끝나면 필요 없다
        LoadedData x;
        if (!files.load(DC_DATA_DIR, &x.cfg, &x.table, &x.hero, &x.meta)) {
            files.report();
            std::abort();
        }
        return x;
    }();
    return loaded;
}

}  // namespace dc::dev

#endif  // DC_DATA_FILES_H
