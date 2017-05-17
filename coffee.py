import importlib
import base
importlib.reload(base)
import collections
import re
import subprocess
import time


ALIASES = {
        'home': 'run 6s w 2s e 2n;open w;w',
        'rt vassendar': 'run 4s d w d 2w d 2n 2e;open s;s;open d;run 5d;open w;w;run 8n w 2s 6w;open w;run 11w 3n 3w;open w;run 5w;run 3n 5w',
        'rt wgate': 'run 2s 3w;open w;w',
        'rt sehaire': 'run w u 6w 2n 3w s 6w s 6w 2n 5w 5n w n w n 4w n e',
        }
TRIGGERS = {
        'You feel a little cleaner, but are still very dirty.': 'bathe',
        'You feel a little cleaner.': 'bathe',
        'You feel a little cleaner; almost perfect.': 'bathe',
        'You are no longer hungry.': '!',
        'You are no longer thirsty.': '!',
        'You are starved, and near death.  EAT SOMETHING!': 'quit;y',
        'You are dehydrated, and near death.  DRINK SOMETHING!': 'quit;y',
        'YOU ARE DYING OF THIRST!': 'quit;y',
        'YOU ARE DYING OF HUNGER!': 'quit;y',
        }
NOTIFICATIONS = {
        }


class Coffee(base.BaseClient):
    def __init__(self, mud):
        super().__init__(mud)
        self.aliases.update(ALIASES)
        self.triggers.update(TRIGGERS)
        self.notifications.update(NOTIFICATIONS)

    def get_host_port():
        return 'coffeemud.net', 23

    def trigger2(self, line):
        if re.match('^You start .+', line):
            self.state['job_start'] = time.time()
        elif re.match('^You are done .+', line) and 'job_start' in self.state:
            self.log('Job took {} seconds'.format(time.time() - self.state['job_start']))



def get_class():
    return Coffee
