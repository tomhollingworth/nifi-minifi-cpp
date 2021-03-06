/**
 * @file MergeContent.cpp
 * MergeContent class implementation
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "MergeContent.h"
#include <stdio.h>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include <queue>
#include <map>
#include <deque>
#include <utility>
#include <algorithm>
#include <numeric>
#include "utils/TimeUtil.h"
#include "utils/StringUtils.h"
#include "utils/GeneralUtils.h"
#include "core/ProcessContext.h"
#include "core/ProcessSession.h"

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace processors {

core::Property MergeContent::MergeStrategy(
  core::PropertyBuilder::createProperty("Merge Strategy")
  ->withDescription("Defragment or Bin-Packing Algorithm")
  ->withAllowableValues<std::string>({merge_content_options::MERGE_STRATEGY_DEFRAGMENT, merge_content_options::MERGE_STRATEGY_BIN_PACK})
  ->withDefaultValue(merge_content_options::MERGE_STRATEGY_DEFRAGMENT)->build());
core::Property MergeContent::MergeFormat(
  core::PropertyBuilder::createProperty("Merge Format")
  ->withDescription("Merge Format")
  ->withAllowableValues<std::string>({merge_content_options::MERGE_FORMAT_CONCAT_VALUE, merge_content_options::MERGE_FORMAT_TAR_VALUE, merge_content_options::MERGE_FORMAT_ZIP_VALUE})
  ->withDefaultValue(merge_content_options::MERGE_FORMAT_CONCAT_VALUE)->build());
core::Property MergeContent::CorrelationAttributeName("Correlation Attribute Name", "Correlation Attribute Name", "");
core::Property MergeContent::DelimiterStrategy(
  core::PropertyBuilder::createProperty("Delimiter Strategy")
  ->withDescription("Determines if Header, Footer, and Demarcator should point to files")
  ->withAllowableValues<std::string>({merge_content_options::DELIMITER_STRATEGY_FILENAME, merge_content_options::DELIMITER_STRATEGY_TEXT})
  ->withDefaultValue(merge_content_options::DELIMITER_STRATEGY_FILENAME)->build());
core::Property MergeContent::Header("Header File", "Filename specifying the header to use", "");
core::Property MergeContent::Footer("Footer File", "Filename specifying the footer to use", "");
core::Property MergeContent::Demarcator("Demarcator File", "Filename specifying the demarcator to use", "");
core::Property MergeContent::KeepPath(
  core::PropertyBuilder::createProperty("Keep Path")
  ->withDescription("If using the Zip or Tar Merge Format, specifies whether or not the FlowFiles' paths should be included in their entry")
  ->withDefaultValue(false)->build());
core::Property MergeContent::AttributeStrategy(
  core::PropertyBuilder::createProperty("Attribute Strategy")
  ->withDescription("Determines which FlowFile attributes should be added to the bundle. If 'Keep All Unique Attributes' is selected, "
                    "any attribute on any FlowFile that gets bundled will be kept unless its value conflicts with the value from another FlowFile "
                    "(in which case neither, or none, of the conflicting attributes will be kept). If 'Keep Only Common Attributes' is selected, "
                    "only the attributes that exist on all FlowFiles in the bundle, with the same value, will be preserved.")
  ->withAllowableValues<std::string>({merge_content_options::ATTRIBUTE_STRATEGY_KEEP_COMMON, merge_content_options::ATTRIBUTE_STRATEGY_KEEP_ALL_UNIQUE})
  ->withDefaultValue(merge_content_options::ATTRIBUTE_STRATEGY_KEEP_COMMON)->build());
core::Relationship MergeContent::Merge("merged", "The FlowFile containing the merged content");
const char *BinaryConcatenationMerge::mimeType = "application/octet-stream";
const char *TarMerge::mimeType = "application/tar";
const char *ZipMerge::mimeType = "application/zip";

void MergeContent::initialize() {
  // Set the supported properties
  std::set<core::Property> properties;
  properties.insert(MinSize);
  properties.insert(MaxSize);
  properties.insert(MinEntries);
  properties.insert(MaxEntries);
  properties.insert(MaxBinAge);
  properties.insert(MaxBinCount);
  properties.insert(MergeStrategy);
  properties.insert(MergeFormat);
  properties.insert(CorrelationAttributeName);
  properties.insert(DelimiterStrategy);
  properties.insert(Header);
  properties.insert(Footer);
  properties.insert(Demarcator);
  properties.insert(KeepPath);
  properties.insert(AttributeStrategy);
  setSupportedProperties(properties);
  // Set the supported relationships
  std::set<core::Relationship> relationships;
  relationships.insert(Original);
  relationships.insert(Failure);
  relationships.insert(Merge);
  setSupportedRelationships(relationships);
}

std::string MergeContent::readContent(std::string path) {
  std::string contents;
  std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
  if (in) {
    in.seekg(0, std::ios::end);
    contents.resize(gsl::narrow<size_t>(in.tellg()));
    in.seekg(0, std::ios::beg);
    in.read(&contents[0], contents.size());
    in.close();
  }
  return (contents);
}

void MergeContent::onSchedule(core::ProcessContext *context, core::ProcessSessionFactory *sessionFactory) {
  std::string value;
  BinFiles::onSchedule(context, sessionFactory);
  if (context->getProperty(MergeStrategy.getName(), value) && !value.empty()) {
    mergeStrategy_ = value;
  }
  if (context->getProperty(MergeFormat.getName(), value) && !value.empty()) {
    mergeFormat_ = value;
  }
  if (context->getProperty(CorrelationAttributeName.getName(), value) && !value.empty()) {
    correlationAttributeName_ = value;
  }
  if (context->getProperty(DelimiterStrategy.getName(), value) && !value.empty()) {
    delimiterStrategy_ = value;
  }
  if (context->getProperty(Header.getName(), value) && !value.empty()) {
    header_ = value;
  }
  if (context->getProperty(Footer.getName(), value) && !value.empty()) {
    footer_ = value;
  }
  if (context->getProperty(Demarcator.getName(), value) && !value.empty()) {
    demarcator_ = value;
  }
  if (context->getProperty(KeepPath.getName(), value) && !value.empty()) {
    org::apache::nifi::minifi::utils::StringUtils::StringToBool(value, keepPath_);
  }
  if (context->getProperty(AttributeStrategy.getName(), value) && !value.empty()) {
    attributeStrategy_ = value;
  }

  validatePropertyOptions();

  if (mergeStrategy_ == merge_content_options::MERGE_STRATEGY_DEFRAGMENT) {
    binManager_.setFileCount(FRAGMENT_COUNT_ATTRIBUTE);
  }
  logger_->log_debug("Merge Content: Strategy [%s] Format [%s] Correlation Attribute [%s] Delimiter [%s]", mergeStrategy_, mergeFormat_, correlationAttributeName_, delimiterStrategy_);
  logger_->log_debug("Merge Content: Footer [%s] Header [%s] Demarcator [%s] KeepPath [%d]", footer_, header_, demarcator_, keepPath_);
  if (delimiterStrategy_ == merge_content_options::DELIMITER_STRATEGY_FILENAME) {
    if (!header_.empty()) {
      headerContent_ = readContent(header_);
    }
    if (!footer_.empty()) {
       footerContent_ = readContent(footer_);
    }
    if (!demarcator_.empty()) {
        demarcatorContent_ = readContent(demarcator_);
    }
  }
  if (delimiterStrategy_ == merge_content_options::DELIMITER_STRATEGY_TEXT) {
    headerContent_ = header_;
    footerContent_ = footer_;
    demarcatorContent_ = demarcator_;
  }
}

void MergeContent::validatePropertyOptions() {
  if (mergeStrategy_ != merge_content_options::MERGE_STRATEGY_DEFRAGMENT &&
      mergeStrategy_ != merge_content_options::MERGE_STRATEGY_BIN_PACK) {
    logger_->log_error("Merge strategy not supported %s", mergeStrategy_);
    throw minifi::Exception(ExceptionType::PROCESSOR_EXCEPTION, "Invalid merge strategy: " + attributeStrategy_);
  }

  if (mergeFormat_ != merge_content_options::MERGE_FORMAT_CONCAT_VALUE &&
      mergeFormat_ != merge_content_options::MERGE_FORMAT_TAR_VALUE &&
      mergeFormat_ != merge_content_options::MERGE_FORMAT_ZIP_VALUE) {
    logger_->log_error("Merge format not supported %s", mergeFormat_);
    throw minifi::Exception(ExceptionType::PROCESSOR_EXCEPTION, "Invalid merge format: " + mergeFormat_);
  }

  if (delimiterStrategy_ != merge_content_options::DELIMITER_STRATEGY_FILENAME &&
      delimiterStrategy_ != merge_content_options::DELIMITER_STRATEGY_TEXT) {
    logger_->log_error("Delimiter strategy not supported %s", delimiterStrategy_);
    throw minifi::Exception(ExceptionType::PROCESSOR_EXCEPTION, "Invalid delimiter strategy: " + delimiterStrategy_);
  }

  if (attributeStrategy_ != merge_content_options::ATTRIBUTE_STRATEGY_KEEP_COMMON &&
      attributeStrategy_ != merge_content_options::ATTRIBUTE_STRATEGY_KEEP_ALL_UNIQUE) {
    logger_->log_error("Attribute strategy not supported %s", attributeStrategy_);
    throw minifi::Exception(ExceptionType::PROCESSOR_EXCEPTION, "Invalid attribute strategy: " + attributeStrategy_);
  }
}

std::string MergeContent::getGroupId(core::ProcessContext*, std::shared_ptr<core::FlowFile> flow) {
  std::string groupId = "";
  std::string value;
  if (!correlationAttributeName_.empty()) {
    if (flow->getAttribute(correlationAttributeName_, value))
      groupId = value;
  }
  if (groupId.empty() && mergeStrategy_ == merge_content_options::MERGE_STRATEGY_DEFRAGMENT) {
    if (flow->getAttribute(FRAGMENT_ID_ATTRIBUTE, value))
      groupId = value;
  }
  return groupId;
}

bool MergeContent::checkDefragment(std::unique_ptr<Bin> &bin) {
  std::deque<std::shared_ptr<core::FlowFile>> &flows = bin->getFlowFile();
  if (!flows.empty()) {
    std::shared_ptr<core::FlowFile> front = flows.front();
    std::string fragId;
    if (!front->getAttribute(BinFiles::FRAGMENT_ID_ATTRIBUTE, fragId))
      return false;
    std::string fragCount;
    if (!front->getAttribute(BinFiles::FRAGMENT_COUNT_ATTRIBUTE, fragCount))
      return false;
    int fragCountInt;
    try {
      fragCountInt = std::stoi(fragCount);
    }
    catch (...) {
      return false;
    }
    for (auto flow : flows) {
      std::string value;
      if (!flow->getAttribute(BinFiles::FRAGMENT_ID_ATTRIBUTE, value))
          return false;
      if (value != fragId)
        return false;
      if (!flow->getAttribute(BinFiles::FRAGMENT_COUNT_ATTRIBUTE, value))
        return false;
      if (value != fragCount)
        return false;
      if (!flow->getAttribute(BinFiles::FRAGMENT_INDEX_ATTRIBUTE, value))
        return false;
      try {
        int index = std::stoi(value);
        if (index < 0 || index >= fragCountInt)
          return false;
      }
      catch (...) {
        return false;
      }
    }
  } else {
    return false;
  }

  return true;
}

void MergeContent::onTrigger(core::ProcessContext *context, core::ProcessSession *session) {
  BinFiles::onTrigger(context, session);
}

bool MergeContent::processBin(core::ProcessContext *context, core::ProcessSession *session, std::unique_ptr<Bin> &bin) {
  if (mergeStrategy_ != merge_content_options::MERGE_STRATEGY_DEFRAGMENT && mergeStrategy_ != merge_content_options::MERGE_STRATEGY_BIN_PACK)
    return false;

  if (mergeStrategy_ == merge_content_options::MERGE_STRATEGY_DEFRAGMENT) {
    // check the flowfile fragment values
    if (!checkDefragment(bin)) {
      logger_->log_error("Merge Content check defgrament failed");
      return false;
    }
    // sort the flowfile fragment index
    std::deque<std::shared_ptr<core::FlowFile>> &flows = bin->getFlowFile();
    std::sort(flows.begin(), flows.end(), [] (const std::shared_ptr<core::FlowFile> &first, const std::shared_ptr<core::FlowFile> &second)
        {std::string value;
         first->getAttribute(BinFiles::FRAGMENT_INDEX_ATTRIBUTE, value);
         int indexFirst = std::stoi(value);
         second->getAttribute(BinFiles::FRAGMENT_INDEX_ATTRIBUTE, value);
         int indexSecond = std::stoi(value);
         if (indexSecond > indexFirst)
           return true;
         else
           return false;
        });
  }

  std::shared_ptr<core::FlowFile> merge_flow = std::static_pointer_cast<FlowFileRecord>(session->create());
  if (attributeStrategy_ == merge_content_options::ATTRIBUTE_STRATEGY_KEEP_COMMON)
    KeepOnlyCommonAttributesMerger(bin->getFlowFile()).mergeAttributes(session, merge_flow);
  else if (attributeStrategy_ == merge_content_options::ATTRIBUTE_STRATEGY_KEEP_ALL_UNIQUE)
    KeepAllUniqueAttributesMerger(bin->getFlowFile()).mergeAttributes(session, merge_flow);
  else {
    logger_->log_error("Attribute strategy not supported %s", attributeStrategy_);
    return false;
  }

  std::unique_ptr<MergeBin> mergeBin;
  if (mergeFormat_ == merge_content_options::MERGE_FORMAT_CONCAT_VALUE)
    mergeBin = utils::make_unique<BinaryConcatenationMerge>();
  else if (mergeFormat_ == merge_content_options::MERGE_FORMAT_TAR_VALUE)
    mergeBin = utils::make_unique<TarMerge>();
  else if (mergeFormat_ == merge_content_options::MERGE_FORMAT_ZIP_VALUE)
    mergeBin = utils::make_unique<ZipMerge>();
  else {
    logger_->log_error("Merge format not supported %s", mergeFormat_);
    return false;
  }

  try {
    mergeBin->merge(context, session, bin->getFlowFile(), headerContent_, footerContent_, demarcatorContent_, merge_flow);
  } catch (...) {
    logger_->log_error("Merge Content merge catch exception");
    return false;
  }
  session->putAttribute(merge_flow, BinFiles::FRAGMENT_COUNT_ATTRIBUTE, std::to_string(bin->getSize()));

  // we successfully merge the flow
  session->transfer(merge_flow, Merge);
  std::deque<std::shared_ptr<core::FlowFile>> &flows = bin->getFlowFile();
  for (auto flow : flows) {
    session->transfer(flow, Original);
  }
  logger_->log_info("Merge FlowFile record UUID %s, payload length %d", merge_flow->getUUIDStr(), merge_flow->getSize());

  return true;
}

void BinaryConcatenationMerge::merge(core::ProcessContext*, core::ProcessSession *session,
    std::deque<std::shared_ptr<core::FlowFile>> &flows, std::string &header, std::string &footer, std::string &demarcator,
    const std::shared_ptr<core::FlowFile> &merge_flow) {
  BinaryConcatenationMerge::WriteCallback callback(header, footer, demarcator, flows, session);
  session->write(merge_flow, &callback);
  session->putAttribute(merge_flow, core::SpecialFlowAttribute::MIME_TYPE, getMergedContentType());
  std::string fileName;
  if (flows.size() == 1) {
    flows.front()->getAttribute(core::SpecialFlowAttribute::FILENAME, fileName);
  } else {
    flows.front()->getAttribute(BinFiles::SEGMENT_ORIGINAL_FILENAME, fileName);
  }
  if (!fileName.empty())
    session->putAttribute(merge_flow, core::SpecialFlowAttribute::FILENAME, fileName);
}

void TarMerge::merge(core::ProcessContext*, core::ProcessSession *session, std::deque<std::shared_ptr<core::FlowFile>> &flows, std::string&,
    std::string&, std::string&, const std::shared_ptr<core::FlowFile> &merge_flow) {
  ArchiveMerge::WriteCallback callback(std::string(merge_content_options::MERGE_FORMAT_TAR_VALUE), flows, session);
  session->write(merge_flow, &callback);
  session->putAttribute(merge_flow, core::SpecialFlowAttribute::MIME_TYPE, getMergedContentType());
  std::string fileName;
  merge_flow->getAttribute(core::SpecialFlowAttribute::FILENAME, fileName);
  if (flows.size() == 1) {
    flows.front()->getAttribute(core::SpecialFlowAttribute::FILENAME, fileName);
  } else {
    flows.front()->getAttribute(BinFiles::SEGMENT_ORIGINAL_FILENAME, fileName);
  }
  if (!fileName.empty()) {
    fileName += ".tar";
    session->putAttribute(merge_flow, core::SpecialFlowAttribute::FILENAME, fileName);
  }
}

void ZipMerge::merge(core::ProcessContext*, core::ProcessSession *session, std::deque<std::shared_ptr<core::FlowFile>> &flows, std::string&,
    std::string&, std::string&, const std::shared_ptr<core::FlowFile> &merge_flow) {
  ArchiveMerge::WriteCallback callback(std::string(merge_content_options::MERGE_FORMAT_ZIP_VALUE), flows, session);
  session->write(merge_flow, &callback);
  session->putAttribute(merge_flow, core::SpecialFlowAttribute::MIME_TYPE, getMergedContentType());
  std::string fileName;
  merge_flow->getAttribute(core::SpecialFlowAttribute::FILENAME, fileName);
  if (flows.size() == 1) {
    flows.front()->getAttribute(core::SpecialFlowAttribute::FILENAME, fileName);
  } else {
    flows.front()->getAttribute(BinFiles::SEGMENT_ORIGINAL_FILENAME, fileName);
  }
  if (!fileName.empty()) {
    fileName += ".zip";
    session->putAttribute(merge_flow, core::SpecialFlowAttribute::FILENAME, fileName);
  }
}

void AttributeMerger::mergeAttributes(core::ProcessSession *session, const std::shared_ptr<core::FlowFile> &merge_flow) {
  for (const auto& pair : getMergedAttributes()) {
    session->putAttribute(merge_flow, pair.first, pair.second);
  }
}

std::map<std::string, std::string> AttributeMerger::getMergedAttributes() {
  if (flows_.empty()) return {};
  std::map<std::string, std::string> sum{ flows_.front()->getAttributes() };
  const auto merge_attributes = [this](std::map<std::string, std::string>* const merged_attributes, const std::shared_ptr<core::FlowFile>& flow) {
    processFlowFile(flow, *merged_attributes);
    return merged_attributes;
  };
  return *std::accumulate(std::next(flows_.cbegin()), flows_.cend(), &sum, merge_attributes);
}

void KeepOnlyCommonAttributesMerger::processFlowFile(const std::shared_ptr<core::FlowFile> &flow_file, std::map<std::string, std::string> &merged_attributes) {
  auto flow_attributes = flow_file->getAttributes();
  std::map<std::string, std::string> tmp_merged;
  std::set_intersection(std::make_move_iterator(merged_attributes.begin()), std::make_move_iterator(merged_attributes.end()),
    std::make_move_iterator(flow_attributes.begin()), std::make_move_iterator(flow_attributes.end()), std::inserter(tmp_merged, tmp_merged.begin()));
  merged_attributes = std::move(tmp_merged);
}

void KeepAllUniqueAttributesMerger::processFlowFile(const std::shared_ptr<core::FlowFile> &flow_file, std::map<std::string, std::string> &merged_attributes) {
  auto flow_attributes = flow_file->getAttributes();
  for (auto&& attr : flow_attributes) {
    if(std::find(removed_attributes_.cbegin(), removed_attributes_.cend(), attr.first) != removed_attributes_.cend()) {
      continue;
    }
    std::map<std::string, std::string>::iterator insertion_res;
    bool insertion_happened;
    std::tie(insertion_res, insertion_happened) = merged_attributes.insert(attr);
    if(!insertion_happened && insertion_res->second != attr.second) {
      merged_attributes.erase(insertion_res);
      removed_attributes_.push_back(attr.first);
    }
  }
}

} /* namespace processors */
} /* namespace minifi */
} /* namespace nifi */
} /* namespace apache */
} /* namespace org */
