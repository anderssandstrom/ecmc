"""Optional Tk display. Channel Access runs only in the existing model workers."""
import queue
import threading
import digital
import encoder


class Mailbox:
    """Keep only the latest update per slave when the UI is slower than a model."""
    def __init__(self):
        self.lock = threading.Lock()
        self.pending = {}

    def report(self, path, status, fields):
        with self.lock:
            previous = self.pending.get(path, ('', {}))[1]
            self.pending[path] = (status, fields or previous)

    def take(self):
        with self.lock:
            updates, self.pending = self.pending, {}
        return updates


class Window:
    def __init__(self, root, slaves, prefix, mailbox, commands, stop):
        from tkinter import ttk
        self.root, self.mailbox, self.commands, self.stop = root, mailbox, commands, stop
        self.prefix = prefix
        self.editable = {}
        root.title('ECMC Python simulation — ' + prefix)
        root.geometry('1000x650')
        ttk.Label(root, text=prefix + '   |   Live simulation I/O; drive models run in the background').pack(anchor='w', padx=10, pady=8)
        frame = ttk.Frame(root)
        frame.pack(fill='both', expand=True, padx=10)
        self.tree = ttk.Treeview(frame, columns=('value', 'role'), selectmode='browse')
        self.tree.heading('#0', text='Slave / field')
        self.tree.heading('value', text='Value / model status')
        self.tree.heading('role', text='Hardware / access')
        self.tree.column('#0', width=420)
        self.tree.column('value', width=230)
        self.tree.column('role', width=220)
        scrollbar = ttk.Scrollbar(frame, orient='vertical', command=self.tree.yview)
        self.tree.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side='right', fill='y')
        self.tree.pack(side='left', fill='both', expand=True)
        self.tree.tag_configure('stale', foreground='#777777')
        for slave in slaves:
            layout = digital.LAYOUTS.get(slave.hardware)
            description = (f'{layout[1]}-channel digital {layout[0]}' if layout else
                           f'{encoder.channel_count(slave.hardware)}-channel encoder' if encoder.supported(slave.hardware) else
                           'DS402 drive' if '7062' in slave.hardware or '72' in slave.hardware else 'stepper drive')
            self.tree.insert('', 'end', iid=slave.path, text=f'{slave.path} — {slave.hardware}',
                             values=('connecting', description), open=True)
        controls = ttk.Frame(root)
        controls.pack(fill='x', padx=10, pady=10)
        self.zero = ttk.Button(controls, text='Set 0', command=lambda: self.write(0), state='disabled')
        self.one = ttk.Button(controls, text='Set 1', command=lambda: self.write(1), state='disabled')
        self.zero.pack(side='left')
        self.one.pack(side='left', padx=8)
        self.value = ttk.Entry(controls, width=24)
        self.value.pack(side='left')
        self.apply = ttk.Button(controls, text='Write value', command=self.write_value, state='disabled')
        self.apply.pack(side='left', padx=8)
        self.value.bind('<Return>', lambda _event: self.write_value())
        self.message = ttk.Label(root, text='Select a field. Model/axis/PLC updates may overwrite manual writes.')
        self.message.pack(anchor='w', padx=10, pady=(0, 8))
        self.tree.bind('<<TreeviewSelect>>', self.select)
        root.protocol('WM_DELETE_WINDOW', self.close)
        root.bind('<Destroy>', self.destroyed, add='+')
        self.after_id = root.after(100, self.refresh)

    def select(self, _event=None):
        selected = self.tree.selection()
        enabled = bool(selected and selected[0] in self.editable and not self.stop.is_set())
        for button in (self.zero, self.one, self.apply):
            button.configure(state='normal' if enabled else 'disabled')

    def write_value(self):
        text = self.value.get().strip()
        try:
            try:
                value = int(text, 0)
            except ValueError:
                # Keep large integer words exact; only decimal/exponent syntax uses float.
                if not any(char in text.lower() for char in ('.', 'e')):
                    raise ValueError('enter an integer, hexadecimal word or finite decimal value')
                value = float(text)
            self.write(value)
        except ValueError as error:
            self.message.configure(text=str(error))

    def write(self, value):
        selected = self.tree.selection()
        if selected and selected[0] in self.editable and not self.stop.is_set():
            path, channel = self.editable[selected[0]]
            try:
                self.commands[path].put_nowait((channel, value))
                self.message.configure(text=f'{path}: requested {value}; table shows current readback')
            except queue.Full:
                self.message.configure(text='Write queue full; wait for readback before retrying.')

    def refresh(self):
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None
        for path, (status, fields) in self.mailbox.take().items():
            if not self.tree.exists(path):
                continue
            stale = status.startswith(('error:', 'stopped', 'write failed:'))
            self.tree.set(path, 'value', status)
            for child in self.tree.get_children(path):
                self.tree.item(child, tags=('stale',) if stale else ())
                self.editable.pop(child, None)
            for pvname, (value, channel) in fields.items():
                row = path + '/' + pvname
                name = pvname[len(self.prefix):] if pvname.startswith(self.prefix) else pvname
                shown = str(value)
                if 'driveControl' in name or 'driveStatus' in name or 'encoderStatus' in name:
                    shown += f'  (0x{int(value):04X})'
                role = 'writable simulation field' if channel is not None else 'read-only (no write record/access)'
                if not self.tree.exists(row):
                    self.tree.insert(path, 'end', iid=row, text=name)
                self.tree.item(row, values=(shown, role), tags=('stale',) if stale else ())
                if channel is not None and not stale:
                    self.editable[row] = (path, channel)
        self.select()
        self.after_id = self.root.after(100, self.refresh)

    def destroyed(self, event):
        if event.widget is self.root and self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None

    def close(self):
        self.stop.set()
        self.root.destroy()


def run(slaves, prefix, pv_factory, thread_factory, rate, timeout, duration, settings, runner):
    # Construct the display before starting workers; missing Tk/display causes no writes.
    try:
        import tkinter as tk
        root = tk.Tk()
    except (ImportError, RuntimeError) as error:
        raise RuntimeError('GUI needs tkinter and a graphical display: ' + str(error)) from error
    except Exception as error:
        raise RuntimeError('cannot open simulation window: ' + str(error)) from error
    stop = threading.Event()
    mailbox = Mailbox()
    commands = {slave.path: queue.Queue(maxsize=64) for slave in slaves}
    results = {}
    thread = None
    try:
        Window(root, slaves, prefix, mailbox, commands, stop)
        def work():
            try:
                results.update(runner(slaves, prefix, pv_factory, thread_factory, rate, timeout,
                                      duration, settings, stop, mailbox.report, commands))
            except Exception as error:
                results['gui'] = 1
                for slave in slaves:
                    mailbox.report(slave.path, 'error: ' + str(error), {})
                stop.set()
        thread = thread_factory(target=work, name='sim-models')
        thread.start()
        root.mainloop()
    finally:
        stop.set()
        try:
            root.destroy()
        except tk.TclError:
            pass
        if thread is not None and thread.ident is not None:
            thread.join()
    return int(any(results.values()))
