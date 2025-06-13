// To enable debug printing, define the preprocessor symbol RIFF_MAN_DEBUG_DEFER
#pragma once

#ifdef RIFF_MAN_DEBUG_DEFER
int DeferredReleaser_counter = 0;
#endif

template <typename Lambda>
class DeferredReleaser {
 public:
    DeferredReleaser(Lambda lambda)
        : m_lambda(lambda) {
#ifdef RIFF_MAN_DEBUG_DEFER
            DeferredReleaser_counter++;
#endif
    }

    ~DeferredReleaser() {
        m_lambda();
#ifdef RIFF_MAN_DEBUG_DEFER
       DeferredReleaser_counter--;
       std::printf("Releaser %d executed.\n");
#endif
    }

 private:
    Lambda m_lambda;
};

template <typename Lambda>
DeferredReleaser<Lambda> Defer(Lambda lambda) {
    return DeferredReleaser(lambda);
}
