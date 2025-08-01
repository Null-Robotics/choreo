#![allow(
    clippy::uninlined_format_args,
    clippy::or_fun_call,
    clippy::single_char_pattern
)]

use nu_ansi_term::{Color, Style};
use tracing::{field::Visit, Level};

pub fn now_str() -> String {
    format!(
        "{:02}:{:02}:{:02}.{:03}",
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .expect("Time went backwards")
            .as_secs()
            % 86400
            / 3600,
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .expect("Time went backwards")
            .as_secs()
            % 3600
            / 60,
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .expect("Time went backwards")
            .as_secs()
            % 60,
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .expect("Time went backwards")
            .subsec_millis()
    )
}

#[inline]
fn hone_file(file: &str) -> String {
    let mut string = file.to_string();
    #[cfg(windows)]
    {
        string = string.replace("\\", "/");
    }
    const REPLACEMENTS: [(&str, &str); 2] = [("src-core/", "core/"), ("src-tauri/", "gui/")];
    for (from, to) in REPLACEMENTS.iter() {
        if string.contains(from) {
            string = string.replace(from, to);
        }
    }
    string
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Locations {
    Frontend,
    Scripting,
    Native,
}

#[derive(Default)]
struct MetaVisitor {
    pub line_num: Option<String>,
    pub function: Option<String>,
    pub file_path: Option<String>,
    pub source: Option<String>,
    pub thread: Option<String>,
    pub message: String,
}
impl Visit for MetaVisitor {
    fn record_debug(&mut self, field: &tracing::field::Field, value: &dyn std::fmt::Debug) {
        match field.name() {
            "line" => self.line_num = Some(format!("{:?}", value)),
            "function" => self.function = Some(format!("{:?}", value)),
            "file" => self.file_path = Some(format!("{:?}", value)),
            "source" => self.source = Some(format!("{:?}", value)),
            "thread" => self.thread = Some(format!("{:?}", value)),
            "message" => self.message = format!("{:?}", value),
            _ => (),
        }
    }
}
impl MetaVisitor {
    pub fn file_path(&self) -> Option<&str> {
        self.file_path.as_deref()
    }
    pub fn message(&self) -> String {
        self.message.clone().replace('"', "")
    }
}

fn fill_string(left_aligned: bool, width: usize, mut s: String) -> String {
    if s.len() > width {
        return s;
    }
    if left_aligned {
        s.extend(std::iter::repeat_n(' ', width - s.len()));
    } else {
        let spaces = std::iter::repeat_n(' ', width - s.len());
        s.insert_str(0, &spaces.collect::<String>());
    }
    s
}

pub struct CompactFormatter {
    pub ansicolor: bool,
}

impl<S, N> tracing_subscriber::fmt::FormatEvent<S, N> for CompactFormatter
where
    S: tracing::Subscriber + for<'a> tracing_subscriber::registry::LookupSpan<'a>,
    N: for<'a> tracing_subscriber::fmt::FormatFields<'a> + 'static,
{
    fn format_event(
        &self,
        _ctx: &tracing_subscriber::fmt::FmtContext<'_, S, N>,
        mut writer: tracing_subscriber::fmt::format::Writer<'_>,
        event: &tracing::Event<'_>,
    ) -> std::fmt::Result {
        let meta = event.metadata();

        let mut visitor = MetaVisitor::default();
        event.record(&mut visitor);

        let loc = match &visitor.source {
            Some(loc) => {
                if loc.contains("scripting") {
                    Locations::Scripting
                } else if loc.contains("frontend") {
                    Locations::Frontend
                } else {
                    Locations::Native
                }
            }
            _ => Locations::Native,
        };

        let level_str = match *meta.level() {
            Level::TRACE => "TRACE",
            Level::DEBUG => "DEBUG",
            Level::INFO => "INFO ",
            Level::WARN => "WARN ",
            Level::ERROR => "ERROR",
        };

        let style = match *meta.level() {
            Level::TRACE => Style::new().fg(Color::Purple),
            Level::DEBUG => Style::new().fg(Color::Blue),
            Level::INFO => Style::new().fg(Color::Green),
            Level::WARN => Style::new().fg(Color::Yellow),
            Level::ERROR => Style::new().fg(Color::Red),
            // _ => return Ok(()),
        };

        let now_str = now_str();

        // let target = match loc {
        //     Locations::Frontend => "choreo::frontend",
        //     Locations::Scripting => "choreo::scripting",
        //     Locations::Native => meta.target(),
        // };

        let message = visitor.message();

        let mut target = String::new();

        let file = hone_file(
            &visitor
                .file_path()
                .unwrap_or(meta.file().unwrap_or("unknown"))
                .to_owned()
                .replace("\"", ""),
        );
        target.push_str(&file);

        if loc != Locations::Native {
            if let Some(line) = visitor.line_num {
                target.push_str(&format!(":{}", line.replace("\"", "")));
            } else if let Some(function) = visitor.function {
                target.push_str(&format!("@{}", function.replace("\"", "")));
            }
        } else if let Some(line) = meta.line() {
            target.push_str(&format!(":{}", line));
        }

        //write LEVEL TIME FILE:LINE MESSAGE

        if self.ansicolor {
            write!(writer, "{}| ", style.paint(level_str))?;
        } else {
            write!(writer, "{}| ", level_str)?;
        }
        write!(writer, "{}| ", now_str)?;
        // write!(writer, "{}| ", fill_string(true, 22, target.to_string()))?;
        write!(writer, "{}| ", fill_string(true, 32, target))?;
        write!(writer, "{}", message)?;
        writer.write_char('\n')?;

        Ok(())
    }
}
