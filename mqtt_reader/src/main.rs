use log::{info, error};
extern crate paho_mqtt as mqtt;
use std::path::{Path, PathBuf};
use std::process;


fn certificate_collection_path() -> &'static Path {
    Path::new("/home/troels/src/esp32-wifi-updates/certificate-collection/")
}

fn ca_path() -> PathBuf {
    return certificate_collection_path().join(Path::new("ca/ca.pem"));
}

fn keystore_path() -> PathBuf {
    return certificate_collection_path()
        .join(Path::new("certificates/temperature_sensor.keystore"));
}

fn main() {
    env_logger::init();

    let conn = rusqlite::Connection::open("db.sqlite").unwrap_or_else( |err| {
        error!("Failed to open sqlite: {}", err);
        process::exit(1);
    });
    conn.execute(
        "create table if not exists events (
             id integer primary key,
             temperature real,
             relative_humidity real,
             time text)",
        rusqlite::NO_PARAMS).unwrap_or_else( |err| {
            error!("Failed to open sqlite3 database: {}", err);
            process::exit(1);
        });
        
    let opts = mqtt::CreateOptionsBuilder::new()
        .server_uri("ssl://178.128.42.0:8883")
        .client_id("mqtt_reader")
        .finalize();

    // Create a client & define connect options
    let mut cli = mqtt::Client::new(opts).unwrap_or_else(|err| {
        error!("Error creating the client: {}", err);
        process::exit(1);
    });

    let ssl_opts = mqtt::SslOptionsBuilder::new()
        .key_store(keystore_path().to_str().unwrap())
        .trust_store(ca_path().to_str().unwrap())
        .finalize();

    let conn_opts = mqtt::ConnectOptionsBuilder::new()
        .ssl_options(ssl_opts)
        .server_uris(&[String::from("ssl://178.128.42.0:8883")])
        .finalize();

    // Connect and wait for it to complete or fail
    if let Err(e) = cli.connect(conn_opts) {
        error!("Unable to connect: {}", e);
        process::exit(1);
    }

    info!("Connected to server");
    let rx = cli.start_consuming();
    
    if let Err(e) = cli.subscribe("topic/temperature", 1) {
        error!("Failed to subscribe to subject topic/temperature: {}", e);
        process::exit(1)
    }
    
    info!("Waiting for messages");

    for msg in rx.iter() {
        match msg {
            Some(msg) => {
                let res = json::parse(&msg.payload_str());
                if let Err(err) = res {
                    error!("Malformed json: {}", err);
                    continue;
                }
                let payload = res.unwrap();
                let temp = payload["temperature"].as_f64();
                let relative_humidity = payload["relative_humidity"].as_f64();
                let time = payload["time"].as_str();

                let insert_res = conn.execute(
                    "insert into events (temperature, relative_humidity, time)
                     values (?1, ?2, ?3)",
                    &[&temp as &dyn rusqlite::ToSql, &relative_humidity, &time]
                );

                if let Err(err) = insert_res {
                    error!("Problems inserting into SQLite: {}", err);
                    continue;
                }
            }
            None => ()
        }
    }

    if let Err(err) = cli.disconnect(None) {
        error!("Failed to disconnect from broker: {}", err);
    }
}
