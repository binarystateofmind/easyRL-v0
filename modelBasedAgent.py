import tkinter
from abc import ABC

import agent


class ModelBasedAgent(agent.Agent, ABC):
    displayName = 'Model Based Agent'
