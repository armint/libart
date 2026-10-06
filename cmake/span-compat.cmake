# The pin of span-compat (embedded-libraries/span-compat), the span type of art::span with
# ART_USE_STD_EXPECTED_SPAN=OFF. Included by the top-level CMakeLists.txt (both builds) and by tests/CMakeLists.txt.
# The pin is a commit, because span-compat has no tags yet. A clone of a commit cannot be shallow; the repository is
# small. Point FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT at a local checkout to build without access to the repository.
set(ART_SPAN_COMPAT_GIT_REPOSITORY https://gitlab.vandertogt.org/embedded-libraries/span-compat.git)
set(ART_SPAN_COMPAT_GIT_TAG 15b7f429d1ba45d1d5e092d8c0c4d97c0bc4968c)
