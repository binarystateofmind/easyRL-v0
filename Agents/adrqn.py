from Agents import drqn
import numpy as np
import itertools

class ADRQN(drqn.DRQN):
    displayName = 'ADRQN'

    def __init__(self, *args):
        super().__init__(*args)
        self.memory = ADRQN.ReplayBuffer(self, self.memory_size, self.historylength)

    def getRecentAction(self):
        return self.memory.get_recent_action()

    def choose_action(self, state):
        recent_state = self.getRecentState()
        recent_state = np.concatenate([recent_state[1:], [state]], 0)
        recentRawActions = self.getRecentAction()
        recent_action = [self.create_one_hot(self.action_size, action) for action in recentRawActions]
        qval = self.predict((recent_state, recent_action), False)
        action = np.argmax(qval)
        return action

    def predict(self, state, isTarget):
        state, action = state
        stateShape = (1,) + (self.historylength,) + self.state_size
        actionShape = (1,) + (self.historylength,) + (self.action_size,)
        state = np.reshape(state, stateShape)
        action = np.reshape(action, actionShape)

        if isTarget:
            result = self.target.predict([state, action, self.allMask])
        else:
            result = self.model.predict([state, action, self.allMask])
        return result

    def buildQNetwork(self):
        from tensorflow.python.keras.optimizer_v2.adam import Adam
        from tensorflow.keras.models import Model
        from tensorflow.keras.layers import Input, Dense, Conv2D
        from tensorflow.keras.layers import Flatten, TimeDistributed, LSTM, concatenate, multiply

        input_shape = (self.historylength,) + self.state_size
        inputA = Input(shape=input_shape)
        inputB = Input(shape=(self.historylength, self.action_size))
        inputC = Input(shape=(self.action_size,))

        if len(self.state_size) == 1:
            x = TimeDistributed(Dense(24, activation='relu'))(inputA)
        else:
            x = TimeDistributed(Conv2D(16, 8, strides=4, activation='relu'))(inputA)
            x = TimeDistributed(Conv2D(32, 4, strides=2, activation='relu'))(x)

        x = TimeDistributed(Flatten())(x)
        x = Model(inputs=inputA, outputs=x)

        y = TimeDistributed(Dense(24, activation='relu'))(inputB)
        y = Model(inputs=inputB, outputs=y)

        combined = concatenate([x.output, y.output])

        z = LSTM(256)(combined)
        z = Dense(10, activation='relu')(z)  # fully connected
        z = Dense(10, activation='relu')(z)
        z = Dense(self.action_size)(z)
        outputs = multiply([z, inputC])

        inputs = [inputA, inputB, inputC]
        model = Model(inputs=inputs, outputs=outputs)
        model.compile(loss='mse', optimizer=Adam(lr=0.0001, clipvalue=1))
        return model

    def calculateTargetValues(self, mini_batch):
        X_train = [np.zeros((self.batch_size,) + (self.historylength,) + self.state_size),
                   np.zeros((self.batch_size,) + (self.historylength,) + (self.action_size,)),
                   np.zeros((self.batch_size,) + (self.action_size,))]
        next_states = [np.zeros((self.batch_size,) + (self.historylength,) + self.state_size),
                   np.zeros((self.batch_size,) + (self.historylength,) + (self.action_size,))]

        for index_rep, history in enumerate(mini_batch):
            for histInd, (prevAction, state, action, reward, next_state, isDone) in enumerate(history):
                X_train[0][index_rep][histInd] = state
                next_states[0][index_rep][histInd] = next_state
                X_train[1][index_rep][histInd] = self.create_one_hot(self.action_size, prevAction)
                next_states[1][index_rep][histInd] = self.create_one_hot(self.action_size, action)
            X_train[2][index_rep] = self.create_one_hot(self.action_size, action)

        Y_train = np.zeros((self.batch_size,) + (self.action_size,))
        qnext = self.target.predict(next_states + [self.allBatchMask])
        qnext = np.amax(qnext, 1)

        for index_rep, history in enumerate(mini_batch):
            prevAction, state, action, reward, next_state, isDone = history[-1]
            if isDone:
                Y_train[index_rep][action] = reward
            else:
                Y_train[index_rep][action] = reward + qnext[index_rep] * self.gamma
        return X_train, Y_train


    class ReplayBuffer(drqn.DRQN.ReplayBuffer):
        def __init__(self, *args):
            super().__init__(*args)

            shape = self.learner.state_size
            if len(shape) >= 2:
                self.emptyState = np.array([[[-10000]] * shape[0] for _ in range(shape[1])])
            else:
                self.emptyState = np.array([-10000] * shape[0])
            self.emptyTrans = (self.emptyState, -1, 0, self.emptyState, False)

        def getTransitions(self, startInd):
            result = []
            limit = (self.curIndex-1)%self.maxlength
            for ind in range(startInd, startInd+self.historylength):
                ind %= self.maxlength
                transition = self.transitions[ind]
                if transition is None:
                    break
                prevInd = (ind-1)%self.maxlength
                prevTransition = self.transitions[prevInd]
                prevAction = -1
                if prevTransition and not prevInd == limit and not prevTransition[4]:
                    prevAction = prevTransition[1]
                result.append((prevAction,) + transition)
                if transition[4] or ind == limit:
                    break
            return self.pad(result)

        def pad(self, transitions):
            pad = [(-1, self.emptyState, -1, 0, self.emptyState, False) for _ in
                   range(self.historylength - len(transitions))]
            return pad + transitions

        def get_recent_action(self):
            result = [None] * self.historylength
            resInd = self.historylength - 1
            start = (self.curIndex - 1) % self.maxlength
            transition = self.transitions[start]
            result[resInd] = transition[1]
            resInd -= 1
            for ind in range(start - 1, start - self.historylength, -1):
                ind %= self.maxlength
                transition = self.transitions[ind]
                if not transition or transition[4]:
                    break
                result[resInd] = transition[1]
                resInd -= 1

            while resInd >= 0:
                result[resInd] = -1
                resInd -= 1

            return result