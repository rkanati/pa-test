
#include <pulse/pulseaudio.h>
#include <pthread.h>

#include <Rk/types.hpp>
#include <Rk/guard.hpp>

#include <stdexcept>
#include <iostream>

void on_signal (pa_mainloop_api*, pa_signal_event*, int sig, void* mainloop_raw) {
  std::cerr << "\npa-test: signal " << sig << "; exiting...\n";
  auto mainloop = (pa_mainloop*) mainloop_raw;
  pa_mainloop_quit (mainloop, 128 + sig);
}

class Slot {
  bool            loaded;
  size_t          value;
  pthread_mutex_t mutex;
  pthread_cond_t  cond;

public:
  Slot () :
    loaded (false),
    mutex { },
    cond { }
  {
    pthread_mutex_init (&mutex, nullptr);
    pthread_cond_init (&cond, nullptr);
  }

  size_t get () {
    pthread_mutex_lock (&mutex);
    while (!loaded)
      pthread_cond_wait (&cond, &mutex);

    size_t result = value;

    pthread_mutex_unlock (&mutex);
    return result;
  }

  void post (size_t in) {
    pthread_mutex_lock (&mutex);
    value = in;
    loaded = true;
    pthread_cond_signal (&cond);
    pthread_mutex_unlock (&mutex);
  }
};

class ContextState {
  size_t state = PA_CONTEXT_UNCONNECTED;
  // std::mutex mutex;
  // std::condition_variable cond;

  void notify (pa_context* ctx) {
    // auto lock = std::unique_lock<std::mutex> (mutex);
    state = pa_context_get_state (ctx);
    // cond.notify_one ();
  }

  static void proxy_notify (pa_context* ctx, void* raw) {
    ((ContextState*) raw)->notify (ctx);
  };

public:
  ContextState (pa_context* ctx) {
    pa_context_set_state_callback (ctx, proxy_notify, this);
  }

  size_t get () const {
    // auto lock = std::unique_lock<std::mutex> (mutex);
    return state;
  }

  /*template<typename Pred>
  size_t wait_until (Pred const& p) {
    auto lock = std::unique_lock<std::mutex> (mutex);
    cond.wait (lock, p);
    return state;
  }*/
};

class PA {
  pa_mainloop*     mainloop;
  pa_signal_event* term_handler,
                 * int_handler;
  pa_context*      context;

public:
  PA () {
    mainloop = pa_mainloop_new ();
    if (!mainloop)
      throw std::runtime_error ("pa_mainloop_new failed");
    auto mainloop_guard = Rk::guard (pa_mainloop_free, mainloop);

    auto signal_err = pa_signal_init (pa_mainloop_get_api (mainloop));
    if (signal_err)
      throw std::runtime_error ("pa_signal_init failed");
    auto signal_guard = Rk::guard (pa_signal_done);

    term_handler = pa_signal_new (SIGTERM, on_signal, mainloop);
    if (!term_handler)
      throw std::runtime_error ("pa_signal_new failed");
    auto term_guard = Rk::guard (pa_signal_free, term_handler);

    int_handler = pa_signal_new (SIGINT, on_signal, mainloop);
    if (!int_handler)
      throw std::runtime_error ("pa_signal_new failed");
    auto int_guard = Rk::guard (pa_signal_free, int_handler);

    context = pa_context_new (pa_mainloop_get_api (mainloop), "rk-pa-test");
    if (!context)
      throw std::runtime_error ("pa_context_new failed");
    auto ctx_guard = Rk::guard (pa_context_unref, context);

    auto connect_err = pa_context_connect (context, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
    if (connect_err)
      throw std::runtime_error ("pa_context_connect failed");

    ContextState st (context);
    for (;;) {
      int loop_result = pa_mainloop_iterate (mainloop, false, nullptr);
      if (loop_result < 0)
        throw std::runtime_error ("error in main loop");

      auto state = st.get ();
      if (state == PA_CONTEXT_READY)
        break;
      else if (state == PA_CONTEXT_FAILED)
        throw std::runtime_error ("failed to connect to pulseaudio server");
    }

    relieve (ctx_guard, int_guard, term_guard, signal_guard, mainloop_guard);
  }

  int run () {
    int result = 0;
    pa_mainloop_run (mainloop, &result);
    return result;
  }

  pa_context* get_context () {
    return context;
  }
};

void stream_notify (pa_stream*, void*) {
  // auto state = pa_stream_get_state (stream);
}

template<typename T>
auto iabs (T x) {
  static auto constexpr bits = sizeof (T) * 8 - 1;
  return (x + (x >> bits)) ^ (x >> bits);
}

template<typename T>
auto triangle (T t, T lambda, T amp) {
  auto const shift = 8;
  auto const factor = (amp << shift) / lambda;
  t %= lambda;
  return (factor * (iabs (t - (lambda / 2)) - (lambda / 4))) >> (shift - 1);
}

void stream_write_request (pa_stream* stream, size_t bytes, void*) {
  static i16 pos = 0;

  i16* ptr = nullptr;
  auto err = pa_stream_begin_write (stream, (void**) &ptr, &bytes);
  if (err)
    return;

  for (size_t i = 0; i != bytes / 2; i++, pos = pos+1)
    ptr[i]
      = triangle<i16> (pos, 200 + triangle<i16> (pos, 20000, 400), 400);

  pa_stream_write (stream, ptr, bytes, nullptr, 0, PA_SEEK_RELATIVE);
}

int main () try {
  PA pa;

  pa_sample_spec format { PA_SAMPLE_S16LE, 44100, 1 };
  auto stream = pa_stream_new (pa.get_context (), "playback", &format, nullptr);
  if (!stream)
    throw std::runtime_error ("pa_stream_new failed");

  pa_stream_set_state_callback (stream, stream_notify, nullptr);
  pa_stream_set_write_callback (stream, stream_write_request, nullptr);

  auto connect_err = pa_stream_connect_playback (stream, nullptr, nullptr, PA_STREAM_NOFLAGS, nullptr, nullptr);
  if (connect_err)
    throw std::runtime_error ("pa_stream_connect_playback failed");

  return pa.run ();
}
catch (std::exception const& e) {
  std::cerr << e.what () << '\n';
  return 1;
}

