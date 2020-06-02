from abc import ABC
import numpy as np

from Agents import modelFreeAgent


class QTable(modelFreeAgent.ModelFreeAgent, ABC):
    displayName = 'Q Table'
    newParameters = [modelFreeAgent.ModelFreeAgent.Parameter('Alpha', 0.00, 1.00, 0.01, 0.18, True, True)]
    parameters = modelFreeAgent.ModelFreeAgent.parameters + newParameters

    def __init__(self, *args):
        paramLen = len(QTable.newParameters)
        super().__init__(*args[:-paramLen])
        (self.alpha,) = args[-paramLen:]
        self.qtable = {}

    def getQvalue(self, state, action):
        return self.qtable.get((state, action), 0.0)

    def choose_action(self, state):
        q = [self.getQvalue(state, a) for a in range(self.action_size)]
        maxQ = max(q)
        epsilon = self.min_epsilon + (self.max_epsilon - self.min_epsilon) * np.exp(-self.decay_rate * self.time_steps)
        # TODO: Put epsilon at a level near this
        # if random.random() > epsilon:
        action = q.index(maxQ)
        # else:
        #     action = self.state_size.sample()
        return action

    def __compute_new_q_value(self):
        pass

    def reset(self):
        self.qtable.clear()