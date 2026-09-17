"""Replay production button/capture handlers with real domain data and stub hardware."""
from pathlib import Path
import re
import sys
source = Path(sys.argv[1]).read_text()
def function(name):
    match = re.search(r'^static [^\n]*\b' + name + r'\(', source, re.M)
    assert match, name
    return source[match.start():source.index('\n}\n', match.start()) + 3]
enums = source[source.index('typedef enum {'):source.index('static const char *TAG')]
fixture = Path(__file__).with_name('navigation_ui_fixture.c').read_text()
names = ('total_capture_count', 'start_capture_round', 'start_capture_session',
         'persist_capture', 'persist_discovery', 'persist_buddy', 'persist_evolution',
         'persist_recovery', 'persist_release', 'bestiary_write_task', 'request_bestiary_write',
         'update_aim', 'finish_throw', 'abandon_encounter', 'throw_ball', 'on_button')
functions = '\n'.join(function(n).replace('static bool request_bestiary_write(',
                     'static bool production_request_bestiary_write(') for n in names)
Path(sys.argv[2]).write_text(fixture.replace('/* ENUMS */', enums).replace('/* PRODUCTION */', functions))
