/*
 * hlog - Live log pipeline for hlquery.
 *
 * Copyright (C) 2021-2026, Carlos F. Ferry <carlos.ferry@gmail.com>
 *
 * This file is part of hlog, released under the BSD License version 3.
 * You are free to redistribute and/or modify this software
 * under the terms of the BSD License.
 * For more details, please visit: https://docs.hlquery.com
 */

#include "core/configreader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

ConfigReader::ConfigReader() = default;
ConfigReader::~ConfigReader() = default;

bool ConfigReader::LoadFile(const std::string& filename)
{
     Tags.clear();
     ActiveIncludes.clear();
     ErrorMsg.clear();
     Valid = LoadFileInternal(filename);
     return Valid;
}

bool ConfigReader::LoadFileInternal(const std::string& filename)
{
     std::error_code ec;
     fs::path path = fs::absolute(filename, ec);
     if (ec)
     {
          path = filename;
     }

     const std::string normalized = path.lexically_normal().string();
     if (ActiveIncludes.find(normalized) != ActiveIncludes.end())
     {
          ErrorMsg = "Include loop detected for " + normalized + ".";
          return false;
     }

     std::ifstream input(normalized);
     if (!input.is_open())
     {
          ErrorMsg = "Failed to open config " + normalized + ".";
          return false;
     }

     ActiveIncludes.insert(normalized);

     std::ostringstream buffer;
     std::string line;
     while (std::getline(input, line))
     {
          const std::string trimmed = TrimWhitespace(line);
          if (!trimmed.empty() && trimmed[0] == '#')
          {
               continue;
          }

          buffer << line << "\n";
     }

     const bool parsed = ParseContent(buffer.str(), normalized);
     ActiveIncludes.erase(normalized);
     return parsed;
}

bool ConfigReader::ParseContent(const std::string& content, const std::string& filename)
{
     size_t pos = 0;

     while (true)
     {
          const size_t open = content.find('<', pos);
          if (open == std::string::npos)
          {
               break;
          }

          const size_t close = content.find('>', open + 1);
          if (close == std::string::npos)
          {
               ErrorMsg = "Unterminated tag in " + filename + ".";
               return false;
          }

          std::string token = TrimWhitespace(content.substr(open + 1, close - open - 1));
          pos = close + 1;

          if (token.empty() || token[0] == '/')
          {
               continue;
          }

          const size_t name_end = token.find_first_of(" \t\r\n");
          const std::string name = name_end == std::string::npos ? token : token.substr(0, name_end);
          const std::string attr_text = name_end == std::string::npos ? "" : token.substr(name_end + 1);

          auto tag = std::make_shared<ConfigTag>(name);
          if (!ParseAttributes(attr_text, tag))
          {
               ErrorMsg = "Invalid attributes on tag '" + name + "' in " + filename + ".";
               return false;
          }

          if (name == "include")
          {
               const std::string include_file = tag->GetString("file", "");
               if (!include_file.empty())
               {
                    fs::path include_path(include_file);
                    if (include_path.is_relative())
                    {
                         include_path = fs::path(filename).parent_path() / include_path;
                    }

                    if (!LoadFileInternal(include_path.lexically_normal().string()))
                    {
                         return false;
                    }
               }

               continue;
          }

          Tags[name].push_back(std::move(tag));
     }

     return true;
}

bool ConfigReader::ParseAttributes(const std::string& text, const std::shared_ptr<ConfigTag>& tag)
{
     static const std::regex attribute_pattern(R"(([A-Za-z0-9_:-]+)\s*=\s*\"([^\"]*)\")");

     auto begin = std::sregex_iterator(text.begin(), text.end(), attribute_pattern);
     auto end = std::sregex_iterator();

     size_t consumed = 0;
     auto only_whitespace = [](const std::string& value) {
          return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
               return std::isspace(ch);
          });
     };

     for (auto it = begin; it != end; ++it)
     {
          const auto match_pos = static_cast<size_t>(it->position());
          if (match_pos > consumed && !only_whitespace(text.substr(consumed, match_pos - consumed)))
          {
               return false;
          }

          tag->SetAttribute((*it)[1].str(), (*it)[2].str());
          consumed = match_pos + static_cast<size_t>(it->length());
     }

     return consumed >= text.size() || only_whitespace(text.substr(consumed));
}

std::string ConfigReader::TrimWhitespace(const std::string& text) const
{
     const size_t start = text.find_first_not_of(" \t\r\n");
     if (start == std::string::npos)
     {
          return "";
     }

     const size_t end = text.find_last_not_of(" \t\r\n");
     return text.substr(start, end - start + 1);
}

std::vector<std::shared_ptr<ConfigTag>> ConfigReader::GetTags(const std::string& tagname) const
{
     const auto it = Tags.find(tagname);
     if (it == Tags.end())
     {
          return {};
     }

     return it->second;
}

std::shared_ptr<ConfigTag> ConfigReader::GetTag(const std::string& tagname) const
{
     const auto it = Tags.find(tagname);
     if (it == Tags.end() || it->second.empty())
     {
          return nullptr;
     }

     return it->second.front();
}

std::string ConfigTag::GetString(const std::string& key, const std::string& default_value) const
{
     const auto it = Attributes.find(key);
     return it == Attributes.end() ? default_value : it->second;
}

int ConfigTag::GetInt(const std::string& key, int default_value) const
{
     try
     {
          return std::stoi(GetString(key));
     }
     catch (...)
     {
          return default_value;
     }
}

bool ConfigTag::GetBool(const std::string& key, bool default_value) const
{
     std::string value = GetString(key);
     std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
          return static_cast<char>(std::tolower(ch));
     });

     if (value == "true" || value == "yes" || value == "1" || value == "on")
     {
          return true;
     }

     if (value == "false" || value == "no" || value == "0" || value == "off")
     {
          return false;
     }

     return default_value;
}

double ConfigTag::GetDouble(const std::string& key, double default_value) const
{
     try
     {
          return std::stod(GetString(key));
     }
     catch (...)
     {
          return default_value;
     }
}

int ConfigTag::GetIntRange(const std::string& key, int default_value, int min_value, int max_value) const
{
     return std::clamp(GetInt(key, default_value), min_value, max_value);
}

double ConfigTag::GetDoubleRange(const std::string& key, double default_value, double min_value, double max_value) const
{
     return std::clamp(GetDouble(key, default_value), min_value, max_value);
}

bool ConfigTag::HasAttribute(const std::string& key) const
{
     return Attributes.find(key) != Attributes.end();
}

std::string ConfigTag::GetStringNonEmpty(const std::string& key, const std::string& default_value) const
{
     const std::string value = GetString(key, default_value);
     return value.empty() ? default_value : value;
}

std::string ConfigTag::GetPath(const std::string& key, const std::string& default_value) const
{
     return GetString(key, default_value);
}

unsigned int ConfigTag::GetUnsignedInt(const std::string& key, unsigned int default_value) const
{
     try
     {
          return static_cast<unsigned int>(std::stoul(GetString(key)));
     }
     catch (...)
     {
          return default_value;
     }
}

size_t ConfigTag::GetSize(const std::string& key, size_t default_value) const
{
     std::string value = GetString(key);
     if (value.empty())
     {
          return default_value;
     }

     std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
          return static_cast<char>(std::tolower(ch));
     });

     size_t multiplier = 1;
     if (value.size() >= 2 && value.substr(value.size() - 2) == "kb")
     {
          multiplier = 1024;
          value.resize(value.size() - 2);
     }
     else if (value.size() >= 2 && value.substr(value.size() - 2) == "mb")
     {
          multiplier = 1024 * 1024;
          value.resize(value.size() - 2);
     }
     else if (value.size() >= 2 && value.substr(value.size() - 2) == "gb")
     {
          multiplier = 1024ULL * 1024ULL * 1024ULL;
          value.resize(value.size() - 2);
     }

     try
     {
          return static_cast<size_t>(std::stoull(value) * multiplier);
     }
     catch (...)
     {
          return default_value;
     }
}

void ConfigTag::SetAttribute(const std::string& key, const std::string& value)
{
     Attributes[key] = value;
}
