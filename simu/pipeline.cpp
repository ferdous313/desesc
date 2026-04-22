// See LICENSE for details.

#include "pipeline.hpp"

#include <vector>

#include "config.hpp"
#include "gprocessor.hpp"

IBucket::IBucket(size_t size, Pipeline* p, bool clean) : FastQueue<Dinst*>(size), cleanItem(clean), pipeLine(p) {}

void IBucket::markFetched() {
#ifndef NDEBUG
  I(fetched == false);
  fetched = true;  // Only called once
#endif

  if (!empty()) {
    //    if (top()->getFlowId())
    //      MSG("@%lld: markFetched Bucket[%p]",(long long int)globalClock, this);
  }

  // printf("Pipeline::readyitem::markfetched() complete\n");
  printf("Pipeline::markFetched:: Came from FetchEngine:: Memrequest at @Clockcyle %llu\n", globalClock);
  printf("Pipeline::markfetched::bucket->PipelineID is %llu at @Clockcyle %llu \n", this->getPipelineId(), globalClock);
  printf("Pipeline::markFetched::Now send to pipeline::readyitem at @clock %llu\n", globalClock);
  pipeLine->readyItem(this);
}

bool PipeIBucketLess::operator()(const IBucket* x, const IBucket* y) const { return x->getPipelineId() > y->getPipelineId(); }

Pipeline::Pipeline(size_t s, size_t fetch, int32_t maxReqs)
    : PipeLength(s)
    , bucketPoolMaxSize(s + 1 + maxReqs)
    , MaxIRequests(maxReqs)
    , nIRequests(maxReqs)
    , buffer(s + 1 + maxReqs)
    , transient_buffer(s + 1 + maxReqs)

{
  maxItemCntr = 0;
  minItemCntr = 0;  // next ticket number to serve; should be the lowerest inorder; the outoforder are kept in received.

  bucketPool.reserve(bucketPoolMaxSize);
  I(bucketPool.empty());
  printf("Pipeline::Pipeline:: bucketPoolMaxSize is %ld\n", bucketPoolMaxSize);

  for (size_t i = 0; i < bucketPoolMaxSize; i++) {
    IBucket* ib = new IBucket(fetch + 1, this);  // +1 instructions
    bucketPool.push_back(ib);
  }

  I(bucketPool.size() == bucketPoolMaxSize);
}

Pipeline::~Pipeline() {
  while (!bucketPool.empty()) {
    delete bucketPool.back();
    bucketPool.pop_back();
  }
  while (!buffer.empty()) {
    delete buffer.top();
    buffer.pop();
  }
  while (!received.empty()) {
    delete received.top();
    received.pop();
  }
}

// push fetched Inst(IF->PipelineQ) into PipelineQ
// Buffer is the biggest one: buckets resides inside buffer
void Pipeline::readyItem(IBucket* b) {
  printf("Pipeline::readyitem::Entering readyitem \n");
  b->setClock();
  b->reset_transient();
  nIRequests++;
  // out-of-order pipelineId are kept separately in recieved; latter works on them
  if (b->getPipelineId() != minItemCntr) {
    printf(
        "Pipeline::readyitem-->recieved.push(b) PipelineID != minItemCntr !!!\nbucket->PipelineID is %llu and minItemCntr is %llu "
        "\n",
        b->getPipelineId(),
        minItemCntr);
    printf("Pipeline::readyitem::recived.push(bucket)::not actual !buffer.push() inst  %llu at @clockcycle %llu\n",
           b->top()->getID(),
           globalClock);
    received.push(b);
    return;
  }

  // If the message is received in-order. Do not use the sorting
  // receive structure (remember that a cache can respond
  // out-of-order the memory requests)
  printf("Pipeline::readyitem-->PipelineID == minItemCntr !!!\nbucket->PipelineID is %llu and minItemCntr is %llu \n",
         b->getPipelineId(),
         minItemCntr);
  minItemCntr++;
  printf("Pipeline::readyitem:: minItemCntr++ bucket->PipelineID is %llu and minItemCntr is %llu \n",
         b->getPipelineId(),
         minItemCntr);

  if (b->empty()) {
    printf("Pipeline::readyitem::bufferEmpty buffer size is %lu\n", buffer.size());
    doneItem(b);
  } else {
    buffer.push(b);
    printf("Pipeline::readyitem::buffersize is %lu\n", buffer.size());
    printf("Pipeline::ReadyItem::pushing bucket--into-->buffer:: inst %llu at @clockcycle %llu\n", b->top()->getID(), globalClock);
  }
  // clear received
  clearItems();
}

void Pipeline::clearItems() {
  printf("Pipeline::clearitem::Entering clearitem \n");
  printf("Pipeline::clearitem::minItemCntr :: Before minItemCntr is %llu \n", minItemCntr);
  while (!received.empty()) {
    IBucket* b = received.top();

    if (b->getPipelineId() != minItemCntr) {
      break;
    }

    received.pop();

    printf("Pipeline::clearitem::minItemCntr :: Before minItemCntr is %llu \n", minItemCntr);
    // should be minItemCnt--
    minItemCntr++;
    printf("Pipeline::clearitem::minItemCntr++ ::AFter  minItemCntr is %llu \n", minItemCntr);

    if (b->empty()) {
      doneItem(b);
    } else {
      buffer.push(b);
      flush_transient_inst_from_buffer();
    }
  }
}

void Pipeline::doneItem(IBucket* b) {
  I(b->getPipelineId() < minItemCntr);
  I(b->empty());
  b->clock = 0;
  b->reset_transient();
  printf("Pipeline::doneItem::bucket.empty()-->bucketpool.push() \n");
  printf("Pipeline::flush_buffer::bucket.empty()-->bucketpool.push() \n");

  printf("Pipeline::doneItem:: Before bucketPool Size is %ld and bucketPoolMaxSize is %ld\n", bucketPool.size(), bucketPoolMaxSize);
  bucketPool.push_back(b);
  printf("Pipeline::doneItem:: After bucketPool++ Size is %ld and bucketPoolMaxSize is %ld\n",
         bucketPool.size(),
         bucketPoolMaxSize);
}

bool Pipeline::transient_buffer_empty() { return transient_buffer.empty(); }

IBucket* Pipeline::flush_transient_inst_from_bucket(IBucket* b) { return b; }

void Pipeline::flush_transient_inst_from_buffer() {
  printf("Pipeline::flush_transient_int_from_Pipelinebuffer Entering before new fetch!!!\n");
  while (!buffer.empty()) {
    auto* bucket = buffer.end_data();
    I(bucket);
    // I(!bucket->empty());

    while (!bucket->empty()) {
      auto* dinst = bucket->end_data();
      I(dinst);
      I(!dinst->is_present_in_rob());
      if (dinst->isTransient()) {
        printf("Pipeline::flush_transient_int_from_Pipelinebuffer destroy instID %llu\n", dinst->getID());
        dinst->destroyTransientInst();
        bucket->pop_from_back();
      } else {
        printf("Pipeline::flush_transient_int_from_Pipelinebuffer No inst in PipeLineBuffer \n");
        if (bucket->empty()) {
          doneItem(bucket);
        }
        return;  // Nothing else to do
      }
    }  // while_!bucket_empty buffer.pop();
    if (bucket->empty()) {
      printf("Pipeline::flush_buffer::bucket.empty()-->bucketpool.push() \n");
      I(bucket->empty());
      doneItem(bucket);
      buffer.pop_from_back();
    } else {
      buffer.pop_from_back();
      printf("Pipeline::flush_buffer::!bucket.empty()-->!bucketpool.push() \n");
    }
    // limamustbuffer.pop_from_back();
  }
}

void Pipeline::flush_transient_inst_from_received_bucket() {
  printf("Pipeline::flush_transient_int_from_received_bucket Entering before new fetch!!!\n");
  while (!received.empty()) {
    IBucket* bucket = received.top();
    printf("Pipeline::flush_transient_int_from_Received_bucket New Bucket Started!!! \n");
    if (bucket->getPipelineId() != minItemCntr) {
      printf(
          "Pipeline::flush_transient_int_from_Received_bucket Bucket ended BREAK PipelineID != minItemCntr !!!\nbucket->PipelineID "
          "is %llu and minItemCntr is %llu \n",
          bucket->getPipelineId(),
          minItemCntr);

      bucket->set_transient();
      break;
    }

    if (bucket) {
      while (!bucket->empty()) {
        printf("Pipeline::flush_transient_int_from_Received_bucket Bucket:: bucket->PipelineID is %llu and minItemCntr is %llu \n",
               bucket->getPipelineId(),
               minItemCntr);
        auto* dinst = bucket->end_data();
        I(dinst);
        I(!dinst->is_present_in_rob());
        if (dinst->isTransient()) {
          printf("Pipeline::flush_transient_int_from_received_bucket destroy instID %llu\n", dinst->getID());
          dinst->destroyTransientInst();
          bucket->pop_from_back();
        } else {
          return;  // Nothing else to do
        }
      }  // bucket_empty_while
      printf("Pipeline::flush_transient_int_from_Received_bucket Yahoo!!! 1 Bucket ended:: bucket==empty!!! \n");

      if (bucket->getPipelineId() == minItemCntr) {
        printf(
            "Pipeline::flush_transient_int_from_Received_bucket Bucket minItemcntr++ ::PipelineID == minItemCntr "
            "!!!\nbucket->PipelineID is %llu and minItemCntr is %llu \n",
            bucket->getPipelineId(),
            minItemCntr);
        minItemCntr++;
      }

      if (bucket->getPipelineId() != minItemCntr) {
        printf(
            "Pipeline::flush_transient_int_from_Received_bucket Bucket ended BREAK PipelineID != minItemCntr "
            "!!!\nbucket->PipelineID is %llu and minItemCntr is %llu \n",
            bucket->getPipelineId(),
            minItemCntr);

        bucket->set_transient();
        break;
      }
      if (bucket->empty()) {
        bucket->clock = 0;
        printf("Pipeline::flush_transient_int_from_Received_bucket BucketEmpty-->Push to BucketPOOL!!! \n");
        doneItem(bucket);
      }
      received.pop();
    }  // if_bucket
  }    //! recieved_empty
}

IBucket* Pipeline::nextItem() {
  printf("Pipeline::nextitem::Entering nextitem \n");
  while (1) {
    if (buffer.empty()) {
#ifndef NDEBUG
      // It should not be possible to propagate more buckets
      printf("Pipeline::nextitem::Bufferempty+ so return NULL!!!\n");
      clearItems();
      I(buffer.empty());
#endif
      return nullptr;
    }

    if (((buffer.top())->getClock() + PipeLength) > globalClock) {
      return nullptr;
    }
    IBucket* b = buffer.top();
    buffer.pop();
    // fprintf(stderr,"@%lld: Popping Bucket[%p]\n",(long long int)globalClock ,b);
    I(!b->empty());
    I(!b->cleanItem);

    I(!b->empty());
    I(b->top() != nullptr);

    if (b) {
      Dinst* dinst = b->top();
      printf("Pipeline::nextitem inst  %llu at @clockcycle %llu\n", dinst->getID(), globalClock);
    }
    return b;
  }
}

PipeQueue::PipeQueue(CPU_t i)
    : pipeLine(
        Config::get_integer("soc", "core", i, "decode_delay", 1, 64) + Config::get_integer("soc", "core", i, "rename_delay", 1, 8),
        Config::get_integer("soc", "core", i, "fetch_width", 1, 64), Config::get_integer("soc", "core", i, "ftq_size", 1, 64))
    , instQueue(Config::get_integer("soc", "core", i, "decode_bucket_size", 1, 128)) {}

PipeQueue::~PipeQueue() {
  // do nothing
}

IBucket* Pipeline::newItem() {
  if (nIRequests == 0) {
    printf("Pipeline::Newitem:: No new item:: nIRequests==0 return FALSE::at @clockcycle %llu\n", globalClock);
    return 0;
  }
  if (bucketPool.empty()) {
    printf("Pipeline::Newitem:: No new item ::bucketPool.empty())::return FALSE::at @clockcycle %llu\n", globalClock);
    return 0;
  }

  nIRequests--;

  IBucket* b = bucketPool.back();
  printf("Pipeline::NewItem():: Before bucketPool Size is %ld and bucketPoolMaxSize is %ld\n",
         bucketPool.size(),
         bucketPoolMaxSize);
  bucketPool.pop_back();
  printf("Pipeline::doneItem:: After bucketPool++ Size is %ld and bucketPoolMaxSize is %ld\n",
         bucketPool.size(),
         bucketPoolMaxSize);

  b->setPipelineId(maxItemCntr);

  printf("Pipeline::Newitem:: new item ::at bucket->PipelineId is %llu at @clockcycle %llu\n", maxItemCntr, globalClock);
  maxItemCntr++;

#ifndef NDEBUG
  b->fetched = false;
  I(b->empty());
#endif

  return b;
}

bool Pipeline::hasOutstandingItems() const { return !buffer.empty() || !received.empty() || nIRequests < MaxIRequests; }
