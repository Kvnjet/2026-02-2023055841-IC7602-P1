use std::sync::{Arc, Mutex};
use warp::Filter;

use crate::dns_interceptor::Stats;

//Serializar la API bien
#[derive(serde::Serialize)]
pub struct SerializableStats {
    pub total_requests: usize,
    pub requests_by_domain: std::collections::HashMap<String, usize>,
}

impl From<&Stats> for SerializableStats {
    fn from(stats: &Stats) -> Self {
        SerializableStats {
            total_requests: stats.total_requests,
            requests_by_domain: stats.requests_by_domain.clone(),
        }
    }
}

pub fn api_filter(stats: Arc<Mutex<Stats>>) -> impl Filter<Extract = (impl warp::Reply,), Error = warp::Rejection> + Clone {
    let stats_clone = stats.clone();
    let get_stats = warp::path("stats")
        .and(warp::get())
        .map(move || {
            let stats = stats_clone.lock().unwrap();
            let serializable_stats = SerializableStats::from(&*stats);
            warp::reply::json(&serializable_stats)
        });

    let stats_clone2 = stats.clone();
    let get_total = warp::path("stats")
        .and(warp::path("total"))
        .and(warp::get())
        .map(move || {
            let stats = stats_clone2.lock().unwrap();
            warp::reply::json(&stats.get_total_requests())
        });

    let stats_clone3 = stats.clone();
    let get_domains = warp::path("stats")
        .and(warp::path("domains"))
        .and(warp::get())
        .map(move || {
            let stats = stats_clone3.lock().unwrap();
            warp::reply::json(&stats.get_requests_by_domain())
        });

    get_stats.or(get_total).or(get_domains)
}