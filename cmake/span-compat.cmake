# The pin of span-compat (github.com/armint/span-compat), the span type of art::span with
# ART_USE_STD_EXPECTED_SPAN=OFF. Included by the top-level CMakeLists.txt (both builds) and by tests/CMakeLists.txt.
# The pin is a commit, because span-compat has no tags yet. A clone of a commit cannot be shallow; the repository is
# small. Point FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT at a local checkout to build without access to the repository.
set(ART_SPAN_COMPAT_GIT_REPOSITORY https://github.com/armint/span-compat.git)
set(ART_SPAN_COMPAT_GIT_TAG 3d9f571cf6fb3eb4bf14bd62b3cf9f86fbc95037)
