COMPILER = g++
COMPILER_STANDARD = 15
COMPILER_VERSION = -std=c++2c
COMPILE_FLAGS_COMMON = -DVULKAN_HPP_NO_STRUCT_CONSTRUCTORS -DVULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS -DGLFW_INCLUDE_VULKAN -DGLM_FORCE_RADIANS -DGLM_FORCE_DEPTH_ZERO_TO_ONE -DSTB_IMAGE_IMPLEMENTATION
COMPILER_FLAGS_RELEASE = ${COMPILER_VERSION} -O3 -DNDEBUG ${COMPILE_FLAGS_COMMON}
COMPILER_FLAGS_DEV = ${COMPILER_VERSION} -O0 -g -pg ${COMPILE_FLAGS_COMMON}
COMPILER_FLAGS_VALGRIND = ${COMPILER_VERSION} -O0 -g ${COMPILE_FLAGS_COMMON}

WARNINGS = -fdelete-null-pointer-checks -fstrict-aliasing -fimplicit-constexpr -pedantic -pedantic-errors -Wall -Wextra -Weffc++ -Wcast-align -Wcast-qual -Wctor-dtor-privacy -Wdisabled-optimization -Wformat=2 -Wformat-nonliteral -Wformat-security -Wformat-signedness -Wformat-truncation=2 -Wformat-y2k -Wlogical-op -Wmissing-declarations -Wmissing-include-dirs -Wnull-dereference -Wnoexcept -Woverloaded-virtual -Wredundant-decls -Wshadow -Wsign-conversion -Wsign-promo -Wstrict-null-sentinel -Wswitch-default -Wswitch-enum -Wundef -Wunused -Wunused-const-variable=2 -Wuseless-cast -Wuninitialized -Wstrict-aliasing -Wduplicated-branches -Wtrampolines -Wduplicated-cond -Wbidi-chars=any -Wfloat-equal -Wconversion -Winline -Wzero-as-null-pointer-constant -Wmissing-noreturn -Wsuggest-attribute=pure -Wsuggest-attribute=const -Wsuggest-attribute=malloc -Wsuggest-attribute=cold -Wsuggest-attribute=format -Wmissing-format-attribute -Wpacked -Wunused-macros -Wno-missing-requires -Wno-missing-template-keyword -Wvariadic-macros -Wunsafe-loop-optimizations -Wno-changes-meaning -Wdouble-promotion -Wcomma-subscript -Wdangling-reference -Wsuggest-final-types -Wsuggest-override -Wsuggest-final-methods -Winvalid-constexpr -Wold-style-cast -Wextra-semi -Wenum-conversion -Werror $(if $(filter-out 13,$(COMPILER_STANDARD)), -Wnrvo -Wsuggest-attribute=returns_nonnull)

RELEASE_WARNINGS = $(if $(filter-out 13,$(COMPILER_STANDARD)), -fhardened -Whardened)

DEBUG_WARNINGS = -fsanitize=address -fsanitize=pointer-compare -fsanitize=pointer-subtract -fsanitize=leak -fsanitize=undefined -fsanitize=shift -fsanitize=shift-exponent -fsanitize=shift-base -fsanitize=integer-divide-by-zero -fsanitize=unreachable -fsanitize=vla-bound -fsanitize=null -fsanitize=return -fsanitize=signed-integer-overflow -fsanitize=bounds -fsanitize=bounds-strict -fsanitize=alignment -fsanitize=object-size -fsanitize=float-divide-by-zero -fsanitize=float-cast-overflow -fsanitize=nonnull-attribute -fsanitize=returns-nonnull-attribute -fsanitize=bool -fsanitize=enum -fsanitize=vptr -fsanitize=pointer-overflow -fsanitize=builtin -fsanitize-address-use-after-scope

INCLUDE_FOLDER = include
INCLUDE_ARGUMENT = -I${INCLUDE_FOLDER}
LIBRARIES = -lglfw -lvulkan -ldl -lpthread -lX11 -lXxf86vm -lXrandr -lXi
RESOURCES_FOLDER = resources

SOURCE_FOLDER = src
SOURCES = $(shell find ${SOURCE_FOLDER} -type f -not -path '*/tracy/*.cpp' -name '*.cpp')
INCLUDE_SOURCES = $(shell find ${INCLUDE_FOLDER} -type f \( -name '*.h' -o -name '*.hpp' \))

BUILD_FOLDER = build
RELEASE_FOLDER = release
DEV_FOLDER = dev
OUTPUT_FOLDER_RELEASE = ${BUILD_FOLDER}/${RELEASE_FOLDER}
OUTPUT_FOLDER_DEV = ${BUILD_FOLDER}/${DEV_FOLDER}
OUTPUT_FILE_RELEASE = main
OUTPUT_FILE_DEV = dev

VALGRIND_FLAGS = --leak-check=full --show-leak-kinds=all -s --track-origins=yes --gen-suppressions=all
VALGRIND_FOLDER = valgrind
OUTPUT_FOLDER_VALGRIND = ${BUILD_FOLDER}/${VALGRIND_FOLDER}
OUTPUT_FILE_VALGRIND = valgrind

TRACY_CLIENT_SOURCE = src/tracy/TracyClient.cpp
TRACY_SOURCES = $(SOURCES) $(TRACY_CLIENT_SOURCE)
TRACY_FOLDER = tracy
TRACY_INCLUDE_ARGUMENT = -Isrc/tracy
TRACY_FLAGS = -DTRACY_ENABLE -DTRACY_TIMER_FALLBACK
OUTPUT_FILE_TRACY = tracy-client
OUTPUT_FOLDER_TRACY = ${BUILD_FOLDER}/${TRACY_FOLDER}

TEST_FOLDER = tests
TEST_SOURCES = $(shell find ${TEST_FOLDER} ${SOURCE_FOLDER} -type f -not -path '*/main.cpp' -not -path '*/${TRACY_FOLDER}/*' -name '*.cpp')
TEST_COMPILER_FLAGS = ${COMPILER_VERSION} --coverage -fPIC -O0 -g -fprofile-arcs -ftest-coverage
TEST_INCLUDE_FOLDER =
TEST_INCLUDE_ARGUMENT =
TEST_INTEGRATIONS_FOLDER = ${TEST_FOLDER}/integrations
TEST_INTEGRATIONS_INCLUDE_ARUGMENT = ${TEST_INCLUDE_ARGUMENT}
TEST_INTEGRATIONS_SOURCES = $(shell find ${TEST_INTEGRATIONS_FOLDER} ${SOURCE_FOLDER} -type f -not -path '*/main.cpp' -not -path '*/${TRACY_FOLDER}/*' -name '*.cpp')
TEST_MOCKS_FOLDER = ${TEST_FOLDER}/mocks
TEST_MOCKS_INCLUDE_ARUGMENT = ${TEST_INCLUDE_ARGUMENT}
TEST_MOCKS_SOURCES = $(shell find ${TEST_MOCKS_FOLDER} ${SOURCE_FOLDER} -type f -not -path '*/main.cpp' -not -path '*/${TRACY_FOLDER}/*' -name '*.cpp')
TEST_UNITS_FOLDER = ${TEST_FOLDER}/unit
TEST_UNITS_INCLUDE_ARUGMENT = ${TEST_INCLUDE_ARGUMENT}
TEST_UNITS_SOURCES = $(shell find ${TEST_UNITS_FOLDER} ${SOURCE_FOLDER} -type f -not -path '*/main.cpp' -not -path '*/${TRACY_FOLDER}/*' -name '*.cpp')
TEST_MAIN_FOLDER = ${TEST_FOLDER}/main
TEST_MAIN_INCLUDE_ARUGMENT = ${TEST_INCLUDE_ARGUMENT}
TEST_MAIN_SOURCES = $(shell find ${TEST_MAIN_FOLDER} ${SOURCE_FOLDER} -type f -not -path '*/main.cpp' -not -path '*/${TRACY_FOLDER}/*' -name '*.cpp')
TEST_LIBRARIES = ${LIBRARIES} -lgcov -lgtest -lgmock -lpthread
TEST_RESOURCES = ${RESOURCES_FOLDER}
TEST_REPEAT_COUNT = 1
TEST_BREAK =
TEST_EXECUTION_FLAGS = --gtest_repeat=${TEST_REPEAT_COUNT} --gtest_brief=1 $(if $(TEST_BREAK),--gtest_break_on_failure)
OUTPUT_FOLDER_TEST = ${BUILD_FOLDER}/${TEST_FOLDER}
OUTPUT_FILE_TEST = test

BRANCH_COVERAGE = --rc branch_coverage=true

LCOV_FILES = $(shell find . -name '*.gcno' -o -name '*.gcda' -o -name '*.info')
LCOV_FLAGS = --no-external -c ${BRANCH_COVERAGE} -d . --ignore-errors mismatch,mismatch
OUTPUT_FOLDER_LCOV = ${OUTPUT_FOLDER_TEST}
OUTPUT_FILE_LCOV = coverage.info
LCOV_REMOVE_FILES = '*/${TEST_FOLDER}/*'

GENHTML_OUTPUT_FOLDER = .\/coverage

CLANG_SOURCES = ${SOURCES} ${TEST_SOURCES}
TIDY_COMPILE_FLAGS = --config-file=.clang-tidy
FORMAT_COMPILE_FLAGS = -i -style=file:.clang-format

CPPCHECK_FOLDER = cppcheck
CPPCHECK_COMPILE_FLAGS = --cppcheck-build-dir=${CPPCHECK_FOLDER} --check-level=exhaustive

PROFILE_FOLDER = .\/profiling
PROFILE_ANNOTATIONS_FOLDER = ${PROFILE_FOLDER}/annotations

ANNOTATION_FILES = $(shell find . -maxdepth 1 -type f -name '*-ann')

GPROF_COMPILER_FLAGS = -A -b -p -y
GPROF_FILE = flat.map

OBJECTS_RELEASE = $(SOURCES:${SOURCE_FOLDER}/%.cpp=${OUTPUT_FOLDER_RELEASE}/%.o)
DEPS_RELEASE = $(OBJECTS_RELEASE:.o=.d)
OBJECTS_DEV = $(SOURCES:${SOURCE_FOLDER}/%.cpp=${OUTPUT_FOLDER_DEV}/%.o)
DEPS_DEV = $(OBJECTS_DEV:.o=.d)
OBJECTS_VALGRIND = $(SOURCES:${SOURCE_FOLDER}/%.cpp=${OUTPUT_FOLDER_VALGRIND}/%.o)
DEPS_VALGRIND = $(OBJECTS_VALGRIND:.o=.d)
OBJECTS_TEST_INTEGRATIONS = $(patsubst $(TEST_INTEGRATIONS_FOLDER)/%.cpp, $(OUTPUT_FOLDER_TEST)/%.o, $(filter $(TEST_INTEGRATIONS_FOLDER)/%.cpp, $(TEST_INTEGRATIONS_SOURCES)))
OBJECTS_TEST_MOCKS = $(patsubst $(TEST_MOCKS_FOLDER)/%.cpp, $(OUTPUT_FOLDER_TEST)/%.o, $(filter $(TEST_MOCKS_FOLDER)/%.cpp, $(TEST_MOCKS_SOURCES)))
OBJECTS_TEST_UNITS = $(patsubst $(TEST_UNITS_FOLDER)/%.cpp, $(OUTPUT_FOLDER_TEST)/%.o, $(filter $(TEST_UNITS_FOLDER)/%.cpp, $(TEST_UNITS_SOURCES)))
OBJECTS_TEST_MAIN = $(patsubst $(TEST_MAIN_FOLDER)/%.cpp, $(OUTPUT_FOLDER_TEST)/%.o, $(filter $(TEST_MAIN_FOLDER)/%.cpp, $(TEST_MAIN_SOURCES)))
OBJECTS_TEST_FULL = $(patsubst $(TEST_FOLDER)/%.cpp, $(OUTPUT_FOLDER_TEST)/%.o, $(filter $(TEST_FOLDER)/%.cpp, $(TEST_SOURCES)))
OBJECTS_TEST_SRC = $(patsubst $(SOURCE_FOLDER)/%.cpp, $(OUTPUT_FOLDER_TEST)/%.o, $(filter $(SOURCE_FOLDER)/%.cpp, $(TEST_SOURCES)))
OBJECTS_TEST_INTEGRATIONS_ALL = $(OBJECTS_TEST_INTEGRATIONS) $(OBJECTS_TEST_UNITS) $(OBJECTS_TEST_MAIN) $(OBJECTS_TEST_SRC)
OBJECTS_TEST_MOCKS_ALL = $(OBJECTS_TEST_MOCKS) $(OBJECTS_TEST_UNITS) $(OBJECTS_TEST_MAIN) $(OBJECTS_TEST_SRC)
OBJECTS_TEST_UNITS_ALL = $(OBJECTS_TEST_UNITS) $(OBJECTS_TEST_MAIN) $(OBJECTS_TEST_SRC)
OBJECTS_TEST_ALL = $(OBJECTS_TEST_FULL) $(OBJECTS_TEST_SRC)
DEPS_TEST_INTEGRATIONS = $(OBJECTS_TEST_INTEGRATIONS_ALL:.o=.d)
DEPS_TEST_MOCKS = $(OBJECTS_TEST_MOCKS_ALL:.o=.d)
DEPS_TEST_UNIT = $(OBJECTS_TEST_UNITS_ALL:.o=.d)
DEPS_TEST_MAIN = $(OBJECTS_TEST_MAIN:.o=.d)
DEPS_TEST_ALL = $(OBJECTS_TEST_ALL:.o=.d)
OBJECTS_TRACY = $(TRACY_SOURCES:${SOURCE_FOLDER}/%.cpp=${OUTPUT_FOLDER_TRACY}/%.o)
DEPS_TRACY = $(OBJECTS_TRACY:.o=.d)

TEST_OPTIONS = full
# Conditionally set flags based on TEST_OPTIONS
ifeq ($(TEST_OPTIONS), full)
	OBJECTS_TEST_USING = $(OBJECTS_TEST_ALL)
else ifeq ($(TEST_OPTIONS), integrations)
	OBJECTS_TEST_USING = $(OBJECTS_TEST_INTEGRATIONS_ALL)
else ifeq ($(TEST_OPTIONS), mocks)
	OBJECTS_TEST_USING = $(OBJECTS_TEST_MOCKS_ALL)
else ifeq ($(TEST_OPTIONS), unit)
	OBJECTS_TEST_USING = $(OBJECTS_TEST_UNITS_ALL)
endif

SHADER_DIR = ${RESOURCES_FOLDER}/shaders
SHADERS = $(wildcard ${SHADER_DIR}/*.slang)
SPV_FILES = ${SHADERS:.slang=.spv}

SLANG_COMPILER = slangc
SLANG_FLAGS = -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name

default: compile

$(SHADER_DIR)/%.spv: $(SHADER_DIR)/%.slang
	${SLANG_COMPILER} $< ${SLANG_FLAGS} -entry vertMain -entry fragMain -o $@

shaders: $(SPV_FILES)

${OUTPUT_FOLDER_RELEASE}/%.o: ${SOURCE_FOLDER}/%.cpp
	@dir=$(dir $@); \
	mkdir -p $$dir;
	${COMPILER} ${COMPILER_FLAGS_RELEASE} ${WARNINGS} ${RELEASE_WARNINGS} ${INCLUDE_ARGUMENT} -MMD -MP -c $< -o $@

-include $(DEPS_RELEASE)

compile: shaders $(OBJECTS_RELEASE)
	${COMPILER} ${COMPILER_FLAGS_RELEASE} ${OBJECTS_RELEASE} ${LIBRARIES} ${RELEASE_WARNINGS} -o ${OUTPUT_FOLDER_RELEASE}/${OUTPUT_FILE_RELEASE}
	if [ -d ${RESOURCES_FOLDER} ]; then \
		cp -r ${RESOURCES_FOLDER} ${OUTPUT_FOLDER_RELEASE}; \
	fi

release: compile
	${OUTPUT_FOLDER_RELEASE}/${OUTPUT_FILE_RELEASE}

${OUTPUT_FOLDER_DEV}/%.o: ${SOURCE_FOLDER}/%.cpp
	@dir=$(dir $@); \
	mkdir -p $$dir;
	${COMPILER} ${COMPILER_FLAGS_DEV} ${WARNINGS} ${DEBUG_WARNINGS} ${INCLUDE_ARGUMENT} -MMD -MP -c $< -o $@

-include $(DEPS_DEV)

debug: shaders $(OBJECTS_DEV)
	${COMPILER} ${COMPILER_FLAGS_DEV} ${OBJECTS_DEV} ${LIBRARIES} ${DEBUG_WARNINGS} -o ${OUTPUT_FOLDER_DEV}/${OUTPUT_FILE_DEV}
	if [ -d ${RESOURCES_FOLDER} ]; then \
		cp -r ${RESOURCES_FOLDER} ${OUTPUT_FOLDER_DEV}; \
	fi

dev: debug
	${OUTPUT_FOLDER_DEV}/${OUTPUT_FILE_DEV}
	rm -rf gmon.out

${OUTPUT_FOLDER_VALGRIND}/%.o: ${SOURCE_FOLDER}/%.cpp
	@dir=$(dir $@); \
	mkdir -p $$dir;
	${COMPILER} ${COMPILER_FLAGS_VALGRIND} ${WARNINGS} ${INCLUDE_ARGUMENT} -MMD -MP -c $< -o $@

-include $(DEPS_VALGRIND)

val: shaders $(OBJECTS_VALGRIND)
	${COMPILER} ${COMPILER_FLAGS_VALGRIND} ${OBJECTS_VALGRIND} ${LIBRARIES} -o ${OUTPUT_FOLDER_VALGRIND}/${OUTPUT_FILE_VALGRIND}
	if [ -d ${RESOURCES_FOLDER} ]; then \
		cp -r ${RESOURCES_FOLDER} ${OUTPUT_FOLDER_VALGRIND}; \
	fi

valgrind: val
	valgrind ${VALGRIND_FLAGS} ${OUTPUT_FOLDER_VALGRIND}/${OUTPUT_FILE_VALGRIND}

${OUTPUT_FOLDER_TEST}/%.o: ${TEST_FOLDER}/%.cpp
	@dir=$(dir $@); \
	mkdir -p $$dir;
	${COMPILER} ${TEST_COMPILER_FLAGS} ${WARNINGS} ${INCLUDE_ARGUMENT} ${TEST_INCLUDE_ARGUMENT} -MMD -MP -c $< -o $@

${OUTPUT_FOLDER_TEST}/%.o: ${TEST_INTEGRATIONS_FOLDER}/%.cpp
	@dir=$(dir $@); \
	mkdir -p $$dir;
	${COMPILER} ${TEST_COMPILER_FLAGS} ${WARNINGS} ${INCLUDE_ARGUMENT} ${TEST_INTEGRATIONS_INCLUDE_ARUGMENT} -MMD -MP -c $< -o $@

${OUTPUT_FOLDER_TEST}/%.o: ${TEST_MOCKS_FOLDER}/%.cpp
	@dir=$(dir $@); \
	mkdir -p $$dir;
	${COMPILER} ${TEST_COMPILER_FLAGS} ${WARNINGS} ${INCLUDE_ARGUMENT} ${TEST_MOCKS_INCLUDE_ARUGMENT} -MMD -MP -c $< -o $@

${OUTPUT_FOLDER_TEST}/%.o: ${TEST_UNITS_FOLDER}/%.cpp
	@dir=$(dir $@); \
	mkdir -p $$dir;
	${COMPILER} ${TEST_COMPILER_FLAGS} ${WARNINGS} ${INCLUDE_ARGUMENT} ${TEST_UNITS_INCLUDE_ARUGMENT} -MMD -MP -c $< -o $@

${OUTPUT_FOLDER_TEST}/%.o: ${TEST_MAIN_FOLDER}/%.cpp
	@dir=$(dir $@); \
	mkdir -p $$dir;
	${COMPILER} ${TEST_COMPILER_FLAGS} ${WARNINGS} ${INCLUDE_ARGUMENT} ${TEST_MAIN_INCLUDE_ARUGMENT} -MMD -MP -c $< -o $@

${OUTPUT_FOLDER_TEST}/%.o: ${SOURCE_FOLDER}/%.cpp
	@dir=$(dir $@); \
	mkdir -p $$dir;
	${COMPILER} ${TEST_COMPILER_FLAGS} ${WARNINGS} ${INCLUDE_ARGUMENT} ${TEST_INCLUDE_ARGUMENT} -MMD -MP -c $< -o $@

copy_and_run_tests:
	if [ -d ${RESOURCES_FOLDER} ]; then \
		cp -r ${RESOURCES_FOLDER} ${OUTPUT_FOLDER_TEST}; \
	fi
	${OUTPUT_FOLDER_TEST}/${OUTPUT_FILE_TEST} ${TEST_EXECUTION_FLAGS}

-include $(DEPS_TEST_INTEGRATIONS)
-include $(DEPS_TEST_MOCKS)
-include $(DEPS_TEST_UNIT)
-include $(DEPS_TEST_MAIN)
-include $(DEPS_TEST_ALL)

build_tests: $(OBJECTS_TEST_USING)
	${COMPILER} ${TEST_COMPILER_FLAGS} ${OBJECTS_TEST_USING} ${TEST_LIBRARIES} -o ${OUTPUT_FOLDER_TEST}/${OUTPUT_FILE_TEST}
	${MAKE} copy_and_run_tests

lcov: build_tests
	mkdir -p ${OUTPUT_FOLDER_LCOV}
	lcov ${LCOV_FLAGS} -o ${OUTPUT_FOLDER_LCOV}/${OUTPUT_FILE_LCOV}
	lcov --remove ${OUTPUT_FOLDER_LCOV}/${OUTPUT_FILE_LCOV} ${LCOV_REMOVE_FILES} ${BRANCH_COVERAGE} -o ${OUTPUT_FOLDER_LCOV}/${OUTPUT_FILE_LCOV}

genhtml: lcov
	rm -rf ${GENHTML_OUTPUT_FOLDER}
	mkdir -p ${GENHTML_OUTPUT_FOLDER}
	genhtml ${OUTPUT_FOLDER_LCOV}/${OUTPUT_FILE_LCOV} ${BRANCH_COVERAGE} --output-directory ${GENHTML_OUTPUT_FOLDER}

coverage: genhtml
	cp ${OUTPUT_FOLDER_TEST}/coverage.info ${GENHTML_OUTPUT_FOLDER}/lcov.info

tidy: ${CLANG_SOURCES}
	clang-tidy ${TIDY_COMPILE_FLAGS} $^ -- ${INCLUDE_ARGUMENT} ${TEST_INCLUDE_ARGUMENT}

check: ${CLANG_SOURCES}
	mkdir -p ${CPPCHECK_FOLDER}
	cppcheck ${CPPCHECK_COMPILE_FLAGS} $^ -I ${INCLUDE_FOLDER}/ -I ${TEST_INCLUDE_FOLDER}/

flawfinder: ${CLANG_SOURCES}
	flawfinder $^

analysis: flawfinder check tidy

format: ${CLANG_SOURCES} ${INCLUDE_SOURCES}
	clang-format ${FORMAT_COMPILE_FLAGS} $^

run_doxygen:
	doxygen Doxyfile
	rm -f Doxyfile.bak

docs: run_doxygen
	sphinx-autobuild -b html -Dbreathe_projects.documentation=docs/xml . docs/sphinx/

${OUTPUT_FOLDER_TRACY}/%.o: ${SOURCE_FOLDER}/%.cpp
	@dir=$(dir $@); \
	mkdir -p $$dir;
	${COMPILER} ${TRACY_FLAGS} ${COMPILER_FLAGS_RELEASE} ${TRACY_INCLUDE_ARGUMENT} ${INCLUDE_ARGUMENT} -MMD -MP -c $< -o $@

-include $(DEPS_TRACY)

tracy: $(OBJECTS_TRACY)
	${COMPILER} ${TRACY_FLAGS} ${COMPILER_FLAGS_RELEASE} ${OBJECTS_TRACY} ${LIBRARIES} -o ${OUTPUT_FOLDER_TRACY}/${OUTPUT_FILE_TRACY}
	if [ -d ${RESOURCES_FOLDER} ]; then \
		cp -r ${RESOURCES_FOLDER} ${OUTPUT_FOLDER_TRACY}; \
	fi
	${OUTPUT_FOLDER_TRACY}/${OUTPUT_FILE_TRACY}

gprof: dev
	mkdir -p ${PROFILE_FOLDER}
	mkdir -p ${PROFILE_ANNOTATIONS_FOLDER}
	rm -rf ${ANNOTATION_FILES}
	mv gmon.out ${PROFILE_FOLDER}/gmon.out
	gprof ${GPROF_COMPILER_FLAGS} ${OUTPUT_FOLDER_DEV}/${OUTPUT_FILE_DEV} ${PROFILE_FOLDER}/gmon.out > ${PROFILE_FOLDER}/${GPROF_FILE}

profile: gprof
	mv ${ANNOTATION_FILES} ${PROFILE_ANNOTATIONS_FOLDER}

initialize_repo:
	git clone https://github.com/Phaysik/CPPBase
	cp -ra CPPBase/Base/. .
	rm -rf CPPBase
	mv hooks/* .git/hooks
	rm -rf hooks
	chmod +x .git/hooks/pre-commit
	chmod +x .git/hooks/commit-msg

.PHONY: tidy run_doxygen initialize_repo copy_and_run_test