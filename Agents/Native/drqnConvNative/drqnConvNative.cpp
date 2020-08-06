#include <iostream>
#include <stdlib.h>
#include <math.h>
#include "drqnConvNative.hpp"

using namespace std;

const int layerSize = 64;

DRQN::DRQN(int inStateChannels, int inStateDim1, int inStateDim2, int inActionSize, float inGamma, int inBatchSize, int inMemorySize, int inTargetUpdate, int inHistorySize)
{
  stateChannels = inStateChannels;
  stateDim1 = inStateDim1;
  stateDim2 = inStateDim2;
  actionSize = inActionSize;
  gamma = inGamma;
  batchSize = inBatchSize;
  memorySize = inMemorySize;
  targetUpdate = inTargetUpdate;
  historySize = inHistorySize;

  if (torch::cuda::is_available()) {
    std::cout << "CUDA is available! Training on GPU." << std::endl;
    device = new torch::Device(torch::kCUDA);
  }
  else
  {
    device = new torch::Device(torch::kCPU);
  }

  model = Dueling(stateChannels, stateDim1, stateDim2, actionSize, layerSize, historySize, 16, 32, 8, 4, 4, 2);
  model->to(*device);
  target = Dueling(stateChannels, stateDim1, stateDim2, actionSize, layerSize, historySize, 16, 32, 8, 4, 4, 2);
  target->to(*device);
  
  model_optimizer = new torch::optim::Adam(model->parameters(), torch::optim::AdamOptions(1e-3));

  fullMask = torch::ones({1,actionSize}).to(*device);
  
  replay = new ReplayBuffer(stateChannels*stateDim1*stateDim2, memorySize, batchSize, historySize);
  printf("BUFFERSIZE: %lu\n", sizeof(float)*memorySize*stateChannels*stateDim1*stateDim2);
  
  gamma = 0.99f;
  itCounter = 0;
  checkpoint_counter = 0;

  cout << "Learning rate: " << 1e-3 << ", target update rate: " << targetUpdate << endl;
  cout << "stateSize: (" << stateChannels << ", " << stateDim1 << ", " << stateDim2 << "), actionSize: " << actionSize << ", gamma: " << gamma << endl;
  cout << ", batchSize: " << batchSize << ", memorySize: " << memorySize << ", targetUpdate: " << targetUpdate << endl;
}

int64_t DRQN::chooseAction(float* state)
{
  //cout << "chooseAction" << endl;
  int64_t action;
  float* recent = new float[historySize * stateChannels * stateDim1 * stateDim2];
  replay->recent(recent, state);
  
  Tensor xsingle = torch::from_blob(recent, {1, historySize, stateChannels, stateDim1, stateDim2}).to(*device);
  Tensor ysingle = model->forward(xsingle, fullMask);
  action = ysingle.argmax(1).item().toInt();
    //action = rand()%outputSize;
    //std::cout << "ACTION " << action << " RANDOMED" << std::endl;
  delete[] recent;
  return action;
}

float DRQN::remember(float* state, int64_t action, float reward, int64_t done)
{
  //cout << "remember" << endl;
  float fLoss=0;
  model_optimizer->zero_grad();
  replay->add(state, action, reward, done);
  
  if (replay->curSize >= batchSize)
  {
    float* bStates = new float[batchSize * historySize * stateChannels * stateDim1 * stateDim2];
    int64_t* bActions = new int64_t[batchSize];
    float* bRewards = new float[batchSize];
    float* bNextStates = new float[batchSize * historySize * stateChannels * stateDim1 * stateDim2];
    int64_t* bDones = new int64_t[batchSize];
    
    replay->sample((float*)bStates, (int64_t*)bActions, (float*)bRewards, (float*)bNextStates, (int64_t*)bDones);
    
    Tensor xbatch = torch::from_blob(bStates, {batchSize, historySize, stateChannels, stateDim1, stateDim2}).to(*device);        
    Tensor actionsbatch = torch::from_blob(bActions, {batchSize, 1}, TensorOptions().dtype(kInt64)).to(*device);
    Tensor rewardsbatch = torch::from_blob(bRewards, {batchSize, 1}).to(*device);
    Tensor nextxbatch = torch::from_blob(bNextStates, {batchSize, historySize, stateChannels, stateDim1, stateDim2}).to(*device);
    Tensor donesbatch = torch::from_blob(bDones, {batchSize, 1}, TensorOptions().dtype(kInt64)).to(*device);
    
    Tensor actionsOneHotBatch = (torch::zeros({batchSize, actionSize}).to(*device).scatter_(1, actionsbatch, 1)).to(*device);
    Tensor ybatch = model->forward(xbatch, actionsOneHotBatch);
    Tensor nextybatch = model->forward(nextxbatch, fullMask);
    Tensor nextybatchTarg = target->forward(nextxbatch, fullMask);
    Tensor argmaxes = nextybatch.argmax(1, true);
    Tensor maxes = nextybatchTarg.gather(1, argmaxes);
    Tensor nextvals = rewardsbatch + (1 - donesbatch) * (gamma * maxes);    
    
    Tensor targetbatch = torch::zeros({batchSize, actionSize}).to(*device).scatter_(1, actionsbatch, nextvals);
    
    torch::Tensor loss = torch::mse_loss(ybatch, targetbatch.detach());
    loss.backward();
    model_optimizer->step();
    fLoss = loss.item<float>();
    
    if ((itCounter+1) % targetUpdate == 0)
    {    
      std::stringstream stream;
      torch::save(model, stream);
      torch::load(target, stream);
      std::cout << "target updated" << std::endl;
    }
    
    /*if (itCounter % kCheckpointEvery == 0) {
      // Checkpoint the model and optimizer state.
      torch::save(model, "model-checkpoint.pt");
      torch::save(*model_optimizer, "model-optimizer-checkpoint.pt");
      std::cout << "\n-> checkpoint " << ++checkpoint_counter << '\n';
    }*/
    
    itCounter++;
    
    delete [] bStates;
    delete [] bActions;
    delete [] bRewards;
    delete [] bNextStates;
    delete [] bDones;
  }
  
  return fLoss;
}

void DRQN::save(char* filename)
{
  torch::save(model, filename);
}

void DRQN::load(char* filename)
{
  torch::load(model, filename);
}

std::stringstream* DRQN::memsave()
{
  std::stringstream* mem = new std::stringstream;
  torch::save(model, *mem);
  return mem;
}

void DRQN::memload(std::stringstream* mem)
{
  torch::load(model, *mem);
  delete mem;
}

DRQN::~DRQN()
{
  delete device;
  delete model_optimizer;
  delete replay;
}

DRQN* createDRQN(int stateChannels, int stateDim1, int stateDim2, int actionSize, float gamma, int inBatchSize, int inMemorySize, int inTargetUpdate, int inHistorySize)
{
  return new DRQN(stateChannels, stateDim1, stateDim2, actionSize, gamma, inBatchSize, inMemorySize, inTargetUpdate, inHistorySize);
}

void freeDRQN(DRQN* drqn)
{
  delete drqn;
}

int64_t chooseAction(DRQN* drqn, float* state)
{
  int64_t result = drqn->chooseAction(state);
  return result;
}

float remember(DRQN* drqn, float* state, int64_t action, float reward, int64_t done)
{
  float result = drqn->remember(state, action, reward, done);
  return result;
}

void save(DRQN* drqn, char* filename)
{
  drqn->save(filename);
}

void load(DRQN* drqn, char* filename)
{
  drqn->load(filename);
}

void* memsave(DRQN* drqn)
{
  return (void*)drqn->memsave();
}

void memload(DRQN* drqn, void* mem)
{
  drqn->memload((std::stringstream*)mem);
}

extern "C"
{
  typedef struct DRQN DRQN;
void* createAgentc(int stateChannels, int stateDim1, int stateDim2, int actionSize, float gamma, int inBatchSize, int inMemorySize, int inTargetUpdate, int inHistorySize)
  {
    return (void*)createDRQN(stateChannels, stateDim1, stateDim2, actionSize, gamma, inBatchSize, inMemorySize, inTargetUpdate, inHistorySize);
  }
  
  void freeAgentc(void* drqn)
  {
    freeDRQN((DRQN*)drqn);
  }
  
  int64_t chooseActionc(void* drqn, float* state)
  {
    return chooseAction((DRQN*)drqn, state);
  }
  
  float rememberc(void* drqn, float* state, int64_t action, float reward, int64_t done)
  {
    return remember((DRQN*)drqn, state, action, reward, done);
  }
  
  void savec(void* drqn, char* filename)
  {
    save((DRQN*)drqn, filename);
  }
  
  void loadc(void* drqn, char* filename)
  {
    load((DRQN*)drqn, filename);
  }
  
  void* memsavec(void* drqn)
  {
    return memsave((DRQN*)drqn);
  }
  
  void memloadc(void* drqn, void* mem)
  {
    memload((DRQN*)drqn, mem);
  }
}