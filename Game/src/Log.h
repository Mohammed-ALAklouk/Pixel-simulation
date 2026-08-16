#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace scree {
    struct Log {
        // Ordered by how much gets thrown away -- the file's verdict is the max over
        // all logs, so do not reorder.
        enum class Severity : std::uint8_t {
            Warning,          // value was clamped or ignored, load continues
            RejectMaterial,   // this entry becomes an inert placeholder
            RejectFile,       // nothing is swapped in, the old registry stands
        };

        enum class Type : std::uint8_t {
            FileMissing,       // could not open materials.json
            ParseFailed,       // the document itself is not valid JSON
            MissingField,      // no name, no weight, no target_type
            WrongType,         // a json::exception from inside a try block
            OutOfRange,        // everything ClampField reports
            UnknownReference,  // unknown tag, material, anchor, lifespan_base, scan_sample
            Duplicate,         // two materials sharing a name
            LimitExceeded,     // past MAX_TAGS or MAX_MATERIALS
        };

        std::string message;
        Severity severity;
        Type type;
        int material = -1;     // which entry this came from; -1 when it is file-level


        // From skips the logs already there when the caller started, so a caller can ask
		// what only its own work produced. Leave it at 0 for the verdict on the whole load.
		static Severity Worst(const std::vector<Log>& logs, std::size_t from = 0) {
			Severity worst = Severity::Warning;
			for (std::size_t i = from; i < logs.size(); ++i) {
				if (static_cast<int>(logs[i].severity) > static_cast<int>(worst)) {
					worst = logs[i].severity;
				}
			}
			return worst;
		}



        static std::string FormatLogs(const std::vector<Log>& logs) {
            std::string out = "";
            for (const auto& log : logs)
                out += (log.severity == Log::Severity::Warning ? "[Warning] " : "[Error] ") + log.message + "\n";

            return out;
		}


        // ---- file level -------------------------------------------------------
        // These fire before any material has an id, so material stays -1.

        static Log CreateFileMissing(const std::string& path) {
            return Log{
                .message  = "Failed to open " + path + ".",
                .severity = Severity::RejectFile,
                .type     = Type::FileMissing,
            };
        }

        static Log CreateParseFailed(const std::string& what) {
            return Log{
                .message  = "Failed to parse materials.json: " + what,
                .severity = Severity::RejectFile,
                .type     = Type::ParseFailed,
            };
        }

        static Log CreateOptionalFileMissing(const std::string& path) {
            return Log{
                .message  = "Failed to open " + path + ", continuing without it.",
                .severity = Severity::Warning,
                .type     = Type::FileMissing,
            };
        }

        static Log CreateOptionalParseFailed(const std::string& path, const std::string& what) {
            return Log{
                .message  = "Failed to parse " + path + ": " + what + " Continuing without it.",
                .severity = Severity::Warning,
                .type     = Type::ParseFailed,
            };
        }

        // What is the plural noun -- "tags", "materials".
        static Log CreateLimitExceeded(const std::string& what, int max) {
            return Log{
                .message  = "Too many " + what + " defined. The maximum is " + std::to_string(max) + ".",
                .severity = Severity::RejectMaterial,
                .type     = Type::LimitExceeded,
            };
        }

        // A tag that is not a string leaves a hole in the tag ids, and every material
        // referencing a tag past that hole resolves to the wrong one, so this takes
        // the whole file down rather than one entry.
        static Log CreateBadTagName(const std::string& dump) {
            return Log{
                .message  = "Tag must be a string, got " + dump + ".",
                .severity = Severity::RejectMaterial,
                .type     = Type::WrongType,
            };
        }

        // ---- material identity ------------------------------------------------
        // Raised while assigning ids, so the entry has no id to report yet.

        static Log CreateMissingName() {
            return Log{
                .message  = "A material entry has no name.",
                .severity = Severity::RejectMaterial,
                .type     = Type::MissingField,
            };
        }

        static Log CreateBadMaterialName(const std::string& dump) {
            return Log{
                .message  = "Material name must be a string, got " + dump + ".",
                .severity = Severity::RejectMaterial,
                .type     = Type::WrongType,
            };
        }

        static Log CreateDuplicateName(const std::string& name) {
            return Log{
                .message  = "Duplicate material name: " + name + ".",
                .severity = Severity::RejectMaterial,
                .type     = Type::Duplicate,
            };
        }

        // ---- per material -----------------------------------------------------

        // Detail describes why the field is unusable when it is present;
        // leave it off when the field is simply absent.
        static Log CreateMissingField(int materialID, const std::string& materialName,
                                      const std::string& field, const std::string& detail = "") {
            return Log{
                .message  = Context(materialName) + field + " is required but "
                          + (detail.empty() ? "absent" : detail) + ".",
                .severity = Severity::RejectMaterial,
                .type     = Type::MissingField,
                .material = materialID,
            };
        }

        // What is either the exception text from nlohmann or a plain description of
        // the shape that was expected.
        static Log CreateWrongType(int materialID, const std::string& materialName,
                                   const std::string& field, const std::string& what) {
            return Log{
                .message  = Context(materialName) + field + " has the wrong type: " + what,
                .severity = Severity::RejectMaterial,
                .type     = Type::WrongType,
                .material = materialID,
            };
        }

        static Log CreateOutOfRange(int materialID, const std::string& materialName,
                                    const std::string& field, int value, int min, int max) {
            return Log{
                .message  = Context(materialName) + field + " is " + std::to_string(value)
                          + ", clamped to [" + std::to_string(min) + ", " + std::to_string(max) + "].",
                .severity = Severity::Warning,
                .type     = Type::OutOfRange,
                .material = materialID,
            };
        }

        // nlohmann converts a float to int without throwing, so the value is already
        // truncated by the time anything else sees it.
        static Log CreateNotIntegral(int materialID, const std::string& materialName,
                                     const std::string& field, const std::string& value, int truncated) {
            return Log{
                .message  = Context(materialName) + field + " is " + value + ", truncated to "
                          + std::to_string(truncated) + ".",
                .severity = Severity::Warning,
                .type     = Type::WrongType,
                .material = materialID,
            };
        }

        // Kind names what was being looked up -- "tag", "anchor tag", "material",
        // "target tag", "lifespan_base", "scan_sample".
        static Log CreateUnknownReference(int materialID, const std::string& materialName,
                                         const std::string& kind, const std::string& name) {
            return Log{
                .message  = Context(materialName) + "unknown " + kind + " '" + name + "'.",
                .severity = Severity::RejectMaterial,
                .type     = Type::UnknownReference,
                .material = materialID,
            };
        }

        // A value that is spelled correctly but meaningless where it appears. The
        // field parses, so the material still loads with the value ignored.
        static Log CreateNotAllowed(int materialID, const std::string& materialName,
                                    const std::string& field, const std::string& value,
                                    const std::string& reason) {
            return Log{
                .message  = Context(materialName) + field + " '" + value
                          + "' is not allowed here: " + reason + ".",
                .severity = Severity::Warning,
                .type     = Type::WrongType,
                .material = materialID,
            };
        }

    private:
        static std::string Context(const std::string& materialName) {
            return "Material '" + materialName + "': ";
        }
    };
}
