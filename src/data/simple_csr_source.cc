/*!
 * Copyright 2015 by Contributors
 * \file simple_csr_source.cc
 */
#include <dmlc/base.h>
#include <xgboost/logging.h>
#include "./simple_csr_source.h"

namespace xgboost {
namespace data {

void SimpleCSRSource::Clear() {
  row_data_.clear();
  row_ptr_.resize(1);
  row_ptr_[0] = 0;
  this->info.Clear();
}

void SimpleCSRSource::CopyFrom(DMatrix* src) {
  this->Clear();
  this->info = src->info();
  dmlc::DataIter<RowBatch>* iter = src->RowIterator();
  iter->BeforeFirst();
  cmplx_data = src->GetCmplxFtr();
  cindex = src->GetCmplxIdx();
  while (iter->Next()) {
    const RowBatch &batch = iter->Value();
    for (size_t i = 0; i < batch.size; ++i) {
      RowBatch::Inst inst = batch[i];
      row_data_.insert(row_data_.end(), inst.data, inst.data + inst.length);
      row_ptr_.push_back(row_ptr_.back() + inst.length);
    }
  }
}

void SimpleCSRSource::CopyFrom(dmlc::Parser<uint32_t>* parser) {
  this->Clear();
  while (parser->Next()) {
    const dmlc::RowBlock<uint32_t>& batch = parser->Value();
    if (batch.label != nullptr) {
      info.labels.insert(info.labels.end(), batch.label, batch.label + batch.size);
    }
    if (batch.weight != nullptr) {
      info.weights.insert(info.weights.end(), batch.weight, batch.weight + batch.size);
    }
    CHECK(batch.index != nullptr);
    // update information
    this->info.num_row += batch.size;
    // copy the data over
    for (size_t i = batch.offset[0]; i < batch.offset[batch.size]; ++i) {
      uint32_t index = batch.index[i];
      bst_float fvalue = batch.value == nullptr ? 1.0f : batch.value[i];
      row_data_.push_back(SparseBatch::Entry(index, fvalue));
      this->info.num_col = std::max(this->info.num_col,
                                    static_cast<uint64_t>(index + 1));
    }
    size_t top = row_ptr_.size();
    for (size_t i = 0; i < batch.size; ++i) {
      row_ptr_.push_back(row_ptr_[top - 1] + batch.offset[i + 1] - batch.offset[0]);
    }
  }
  this->info.num_nonzero = static_cast<uint64_t>(row_data_.size());
}

void SimpleCSRSource::LoadBinary(dmlc::Stream* fi) {
  int tmagic;
  CHECK(fi->Read(&tmagic, sizeof(tmagic)) == sizeof(tmagic)) << "invalid input file format";
  CHECK_EQ(tmagic, kMagic) << "invalid format, magic number mismatch";
  CHECK(fi->Read(&cindex, sizeof(cindex)) == sizeof(cindex)) << "invalid input file format";
  info.LoadBinary(fi);
  fi->Read(&row_ptr_);
  fi->Read(&row_data_);
  // A temporary hack to be able to load complex feature
  std::vector<bst_float> cmplxR(0);
  std::vector<bst_float> cmplxI(0);
  cmplxR.resize( info.num_row );
  cmplxI.resize( info.num_row );
  cmplx_data.resize(0);
  if ( cindex != -1 ) {
    fi->Read(dmlc::BeginPtr(cmplxR), info.num_row*sizeof(bst_float));
    fi->Read(dmlc::BeginPtr(cmplxI), info.num_row*sizeof(bst_float));
    cmplx_data.reserve( cmplxR.size() );
    for ( unsigned i = 0; i < cmplxR.size(); i++ )
      cmplx_data.push_back( bst_cmplx( cmplxR[i], cmplxI[i] ) );
  }
}

void SimpleCSRSource::SaveBinary(dmlc::Stream* fo) const {
  int tmagic = kMagic;
  fo->Write(&tmagic, sizeof(tmagic));
  fo->Write(&cindex, sizeof(cindex));
  info.SaveBinary(fo);
  fo->Write(row_ptr_);
  fo->Write(row_data_);
  // A temporary hack to be able to save complex feature
  std::vector<bst_float> cmplxR(0);
  std::vector<bst_float> cmplxI(0);
  cmplxR.reserve( cmplx_data.size() );
  cmplxI.reserve( cmplx_data.size() );
  for ( unsigned i = 0; i < cmplx_data.size(); i++ ) {
    cmplxR.push_back( cmplx_data[i].r );
    cmplxI.push_back( cmplx_data[i].i );
  }
  if ( cindex != -1 ) {
    fo->Write(dmlc::BeginPtr(cmplxR), cmplxR.size()*sizeof(bst_float));
    fo->Write(dmlc::BeginPtr(cmplxI), cmplxR.size()*sizeof(bst_float));
  }
}

void SimpleCSRSource::BeforeFirst() {
  at_first_ = true;
}

bool SimpleCSRSource::Next() {
  if (!at_first_) return false;
  at_first_ = false;
  batch_.size = row_ptr_.size() - 1;
  batch_.base_rowid = 0;
  batch_.ind_ptr = dmlc::BeginPtr(row_ptr_);
  batch_.data_ptr = dmlc::BeginPtr(row_data_);
  batch_.cmplxVals = dmlc::BeginPtr(cmplx_data);
  batch_.useCmplx = (cmplx_data.size() > 0);
  return true;
}

const RowBatch& SimpleCSRSource::Value() const {
  return batch_;
}

}  // namespace data
}  // namespace xgboost
