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

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ConfigTag;

class ConfigReader
{
   public:
     ConfigReader();
     ~ConfigReader();

     bool LoadFile(const std::string& filename);
     std::vector<std::shared_ptr<ConfigTag>> GetTags(const std::string& tagname) const;
     std::shared_ptr<ConfigTag> GetTag(const std::string& tagname) const;

     bool IsValid() const
     {
          return Valid;
     }

     const std::string& GetError() const
     {
          return ErrorMsg;
     }

   private:
     bool LoadFileInternal(const std::string& filename);
     bool ParseContent(const std::string& content, const std::string& filename);
     bool ParseAttributes(const std::string& text, const std::shared_ptr<ConfigTag>& tag);
     std::string TrimWhitespace(const std::string& text) const;

     bool Valid = false;
     std::string ErrorMsg;
     std::unordered_map<std::string, std::vector<std::shared_ptr<ConfigTag>>> Tags;
     std::unordered_set<std::string> ActiveIncludes;
};

class ConfigTag
{
   public:
     explicit ConfigTag(const std::string& name)
          : Name(name)
     {
     }

     const std::string& GetName() const
     {
          return Name;
     }

     std::string GetString(const std::string& key, const std::string& default_value = "") const;
     int GetInt(const std::string& key, int default_value = 0) const;
     bool GetBool(const std::string& key, bool default_value = false) const;
     double GetDouble(const std::string& key, double default_value = 0.0) const;
     int GetIntRange(const std::string& key, int default_value, int min_value, int max_value) const;
     double GetDoubleRange(const std::string& key, double default_value, double min_value, double max_value) const;
     bool HasAttribute(const std::string& key) const;
     std::string GetStringNonEmpty(const std::string& key, const std::string& default_value = "") const;
     std::string GetPath(const std::string& key, const std::string& default_value = "") const;
     unsigned int GetUnsignedInt(const std::string& key, unsigned int default_value = 0) const;
     size_t GetSize(const std::string& key, size_t default_value = 0) const;

     const std::unordered_map<std::string, std::string>& GetAttributes() const
     {
          return Attributes;
     }

     void SetAttribute(const std::string& key, const std::string& value);

   private:
     std::string Name;
     std::unordered_map<std::string, std::string> Attributes;
};
