"""Based on the pattern provided here:
http://python-3-patterns-idioms-test.readthedocs.org/en/latest/StateMachine.html
"""
import json
import time
import traceback
import manager
from collections import deque

class State(object):
	def __init__(self, manager):
		self.parent = manager

	@property
	def status(self):
		return dict()

	def run(self):
		"""Action that must be continuously run while in this state"""
		ts = self.parent.therm.get()
		self.history.append(ts)
		return dict(type="temperature", time=ts.time, temp=ts.temp, output=self.parent.regulator.output)

class Idle(State):
	def __init__(self, manager):
		super(Idle, self).__init__(manager)
		self.history = deque(maxlen=2400) #about 10 minutes worth, 4 samples / sec * 60 sec / min * 10 min

	def ignite(self):
		_ignite(self.parent.regulator, self.parent.notify)
		return Lit, dict(history=self.history)

	def start_profile(self, schedule, start_time=None, interval=5):
		_ignite(self.parent.regulator, self.parent.notify)
		kwargs = dict(history=self.history, 
			schedule=json.loads(schedule), 
			start_time=float(start_time), 
			interval=float(interval)
		)
		return Running, kwargs

class Lit(State):
	def __init__(self, parent, history):
		super(Lit, self).__init__(parent)
		self.history = manager.TempLog(history)

	def set(self, value):
		try:
			self.parent.regulator.set(float(value))
			return dict(type="success")
		except:
			return dict(type="error", msg=traceback.format_exc())

	def start_profile(self, schedule, start_time=None, interval=5):
		kwargs = dict(history=self.history, 
			schedule=json.loads(schedule), 
			start_time=float(start_time), 
			interval=float(interval)
		)
		return Running, kwargs

	def shutoff(self):
		_shutoff(self.parent.regulator, self.parent.notify)
		return Cooling, dict(history=self.history)

class Cooling(State):
	def __init__(self, parent, history):
		super(Cooling, self).__init__(parent)
		self.history = history

	def run(self):
		ts = self.parent.therm.get()
		self.history.append(ts)
		if ts.temp < 50:
			# Direction logged by TempLog
			# fname = time.strftime('%Y-%m-%d_%I:%M%P.log')
			# with open(os.path.join(paths.log_path, fname), 'w') as fp:
			# 	for time, temp in self.history:
			# 		fp.write("%s\t%s\n"%time, temp)
			return Idle
		return dict(type="temperature", time=ts.time, temp=ts.temp)

	def ignite(self):
		_ignite(self.parent.regulator, self.parent.notify)
		return Lit, dict(history=self.history)

	def start_profile(self, schedule, start_time=None, interval=5):
		_ignite(self.parent.regulator, self.parent.notify)
		kwargs = dict(history=self.history, 
			schedule=json.loads(schedule), 
			start_time=float(start_time), 
			interval=float(interval)
		)
		return Running, kwargs

class Running(State):
	def __init__(self, parent, history, start_time=None, **kwargs):
		super(Running, self).__init__(parent)
		self.profile = manager.Profile(therm=self.parent.therm, regulator=self.parent.regulator, 
			callback=self._notify, start_time=start_time, **kwargs)
		self.start_time = self.profile.start_time
		self.history = history

	@property
	def status(self):
		return dict(start_time=self.start_time, schedule=self.profile.schedule)

	def _notify(self, therm, setpoint, out):
		self.parent.notify(dict(
			type="profile",
			temp=therm,
			setpoint=setpoint,
			output=out,
			ts=self.profile.elapsed,
		))

	def run(self):
		if self.profile.completed:
			#self.parent.notify(dict(type="profile",status="complete"))
			print "Profile complete!"
			_shutoff(self.parent.regulator, self.parent.notify)
			return Cooling, dict(history=self.history)

		return super(Running, self).run()

	def pause(self):
		self.profile.stop()
		return Lit, dict(history=self.history)

	def stop_profile(self):
		self.profile.stop()
		_shutoff(self.parent.regulator, self.parent.notify)
		return Cooling, dict(history=self.history)

	def shutoff(self):
		return self.stop_profile()

def _ignite(regulator, notify):
	try:
		regulator.ignite()
		msg = dict(type="success")
	except ValueError:
		msg = dict(type="error", msg="Cannot ignite: regulator not off")
	except Exception as e:
		msg = dict(type="error", error=repr(e), msg=traceback.format_exc())
	notify(msg)

def _shutoff(regulator, notify):
	try:
		regulator.off()
		msg = dict(type="success")
	except ValueError:
		msg = dict(type="error", msg="Cannot shutoff: regulator not lit")
	except Exception as e:
		msg = dict(type="error", error=repr(e), msg=traceback.format_exc())
	notify(msg)