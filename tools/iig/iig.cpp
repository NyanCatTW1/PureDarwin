#include "iig.h"
#include <vector>

static void usage(const char *progname) {
    std::cerr
        << "usage: " << progname
        << " --def <path/to/input.iig> --header <path/to/header.h> --impl "
           "<path/to/source.iig.cpp> -- <clang args>\n"
           "note: The --edits, --log, --framework-name, and "
           "--deployment-target options from Apple iig are not "
           "implemented and will be ignored"
        << std::endl;

    exit(1);
}

void printTranslationUnit(CXTranslationUnit source) {
    CXCursor cursor = clang_getTranslationUnitCursor(source);

    clang_visitChildren(
        cursor,
        [](CXCursor current_cursor, CXCursor parent, CXClientData client_data) {
            CXType cursor_type = clang_getCursorType(current_cursor);

            CXString type_kind_spelling =
                clang_getTypeKindSpelling(cursor_type.kind);
            std::cout << "TypeKind: " << clang_getCString(type_kind_spelling);
            clang_disposeString(type_kind_spelling);

            if (cursor_type.kind ==
                    CXType_Pointer || // If cursor_type is a pointer
                cursor_type.kind ==
                    CXType_LValueReference || // or an LValue Reference (&)
                cursor_type.kind ==
                    CXType_RValueReference) { // or an RValue Reference (&&),
                CXType pointed_to_type = clang_getPointeeType(
                    cursor_type); // retrieve the pointed-to type

                CXString pointed_to_type_spelling = clang_getTypeSpelling(
                    pointed_to_type); // Spell out the entire
                std::cout << "pointing to type: "
                          << clang_getCString(
                                 pointed_to_type_spelling); // pointed-to type
                clang_disposeString(pointed_to_type_spelling);
            } else if (cursor_type.kind == CXType_Record) {
                CXString type_spelling = clang_getTypeSpelling(cursor_type);
                std::cout << ", namely " << clang_getCString(type_spelling);
                clang_disposeString(type_spelling);
            }
            std::cout << "\n";
            return CXChildVisit_Recurse;
        },
        nullptr);

    clang_visitChildren(
        cursor,
        [](CXCursor current_cursor, CXCursor parent, CXClientData client_data) {
            CXType cursor_type = clang_getCursorType(current_cursor);
            CXString cursor_spelling = clang_getCursorSpelling(current_cursor);
            CXSourceRange cursor_range = clang_getCursorExtent(current_cursor);
            std::cout << "Cursor " << clang_getCString(cursor_spelling);

            CXFile file;
            unsigned start_line, start_column, start_offset;
            unsigned end_line, end_column, end_offset;

            clang_getExpansionLocation(clang_getRangeStart(cursor_range), &file,
                                       &start_line, &start_column,
                                       &start_offset);
            clang_getExpansionLocation(clang_getRangeEnd(cursor_range), &file,
                                       &end_line, &end_column, &end_offset);
            std::cout << " spanning lines " << start_line << " to " << end_line;
            clang_disposeString(cursor_spelling);

            std::cout << "\n";
            return CXChildVisit_Recurse;
        },
        nullptr);
}

int main(int argc, const char *argv[]) {
    int exitCode = 0;
    string inputFilePath, headerOutputPath, implOutputPath;
    vector<const char *> clangFlags;
    bool seenDashDash = false;

    for (int i = 1; i < argc; i++) {
        if (seenDashDash) {
            clangFlags.push_back(argv[i]);
        } else if (strequal(argv[i], "--def")) {
            if (++i == argc) {
                std::cerr << "iig: error: --def option requires an argument"
                          << std::endl;
                usage(argv[0]);
            }

            inputFilePath = argv[i];
        } else if (strequal(argv[i], "--header")) {
            if (++i == argc) {
                std::cerr << "iig: error: --header option requires an argument"
                          << std::endl;
                usage(argv[0]);
            }

            headerOutputPath = argv[i];
        } else if (strequal(argv[i], "--impl")) {
            if (++i == argc) {
                std::cerr << "iig: error: --impl option requires an argument"
                          << std::endl;
                usage(argv[0]);
            }

            implOutputPath = argv[i];
        } else if (strequal(argv[i], "--edits") || strequal(argv[i], "--log") ||
                   strequal(argv[i], "--framework-name") ||
                   strequal(argv[i], "--deployment-target")) {
            if (++i == argc) {
                std::cerr << "iig: error: " << argv[i]
                          << " option requires an argument" << std::endl;
                usage(argv[0]);
            }

            // These options are unimplemented and ignored.
        } else if (strequal(argv[i], "--help")) {
            usage(argv[0]);
        } else if (strequal(argv[i], "--")) {
            seenDashDash = true;
        } else {
            std::cerr << "iig: error: unrecognized option: " << argv[i]
                      << std::endl;
            usage(argv[0]);
        }
    }

    if (inputFilePath.empty()) {
        std::cerr << "iig: error: input file not specified" << std::endl;
        usage(argv[0]);
    } else if (headerOutputPath.empty()) {
        std::cerr << "iig: error: output header file not specified"
                  << std::endl;
        usage(argv[0]);
    } else if (implOutputPath.empty()) {
        std::cerr << "iig: error: output source file not specified"
                  << std::endl;
        usage(argv[0]);
    }

    CXIndex index = clang_createIndex(0, 0);

    CXTranslationUnit source = clang_createTranslationUnitFromSourceFile(
        index, inputFilePath.c_str(), clangFlags.size(), clangFlags.data(), 0,
        nullptr);

    size_t num_diags = clang_getNumDiagnostics(source);
    if (num_diags > 0) {
        bool found_error = false;
        for (size_t diag_index = 0; diag_index < num_diags; diag_index++) {
            auto diag = clang_getDiagnostic(source, diag_index);

            const char *severity = nullptr;
            switch (clang_getDiagnosticSeverity(diag)) {
            case CXDiagnostic_Fatal:
                severity = "fatal error";
                break;
            case CXDiagnostic_Error:
                severity = "error";
                found_error = true;
                break;
            case CXDiagnostic_Warning:
                severity = "warning";
                break;
            case CXDiagnostic_Note:
                severity = "note";
                break;
            default:
                assertion_failure("unknown CXDiagnosticSeverity");
                break;
            }

            auto text = clang_formatDiagnostic(
                diag, CXDiagnostic_DisplaySourceLocation);
            std::cerr << "iig: " << severity << ": " << text << std::endl;
            clang_disposeString(text);

            if (clang_getDiagnosticSeverity(diag) == CXDiagnostic_Fatal) {
                exit(1);
            }

            clang_disposeDiagnostic(diag);
        }

        if (found_error)
            exit(1);
    }

    printTranslationUnit(source);

    std::cerr << "iig: fatal error: iig is not implemented at this time."
              << std::endl;
    exitCode = -1;

    clang_disposeIndex(index);
    return exitCode;
}
